#!/usr/bin/env python3
"""Render a posed native GTA hand for visual regression checks."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-dir", required=True, type=Path)
    parser.add_argument("--rwfury-root", required=True, type=Path)
    parser.add_argument("--ifp", required=True, type=Path)
    parser.add_argument("--animation", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--time", type=float, default=2.0 / 3.0)
    parser.add_argument("--hand-local", action="store_true")
    return parser.parse_args()


def quaternion_row_matrix(rotation: tuple[float, ...]) -> np.ndarray:
    x, y, z, w = rotation
    column_matrix = np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ],
        dtype=float,
    )
    return column_matrix.T


def local_matrix(rotation: np.ndarray, position: tuple[float, ...]) -> np.ndarray:
    result = np.eye(4)
    result[:3, :3] = rotation
    result[3, :3] = position
    return result


def normalized_quaternion(rotation: np.ndarray) -> np.ndarray:
    length = np.linalg.norm(rotation)
    return rotation / length if length > 0.0 else np.array([0.0, 0.0, 0.0, 1.0])


def sample_rotation(obj: object, sample_time: float) -> tuple[float, ...]:
    frames = obj.frames
    if sample_time <= frames[0].time:
        return frames[0].rotation
    if sample_time >= frames[-1].time:
        return frames[-1].rotation
    for before, after in zip(frames, frames[1:], strict=False):
        if sample_time <= after.time:
            span = after.time - before.time
            alpha = 0.0 if span <= 0.0 else (sample_time - before.time) / span
            first = np.array(before.rotation, dtype=float)
            second = np.array(after.rotation, dtype=float)
            if np.dot(first, second) < 0.0:
                second = -second
            return tuple(normalized_quaternion(first * (1.0 - alpha) + second * alpha))
    return frames[-1].rotation


def main() -> int:
    args = parse_args()
    sys.path.insert(0, str(args.rwfury_root.resolve()))
    from rwfury import Dff, Ifp, Img

    model_archive = Img.from_file(args.game_dir / "models" / "gta3.img")
    dff = Dff.from_bytes(model_archive.read(args.model))
    package = Ifp.from_file(str(args.ifp))
    animation = package.get_animation(args.animation)
    if animation is None:
        raise SystemExit(f"No existe la animación {args.animation}")

    animation_by_bone = {obj.bone_id: obj for obj in animation.objects}
    bone_id_by_frame = {
        index: frame.hanim.node_id
        for index, frame in enumerate(dff.frames)
        if frame.hanim is not None
    }

    globals_: list[np.ndarray] = []
    for frame_index, frame in enumerate(dff.frames):
        bone_id = bone_id_by_frame.get(frame_index)
        obj = animation_by_bone.get(bone_id)
        rotation = (
            quaternion_row_matrix(sample_rotation(obj, args.time))
            if obj is not None
            else np.array(frame.rotation_matrix, dtype=float).reshape(3, 3)
        )
        local = local_matrix(rotation, frame.position)
        parent = frame.parent
        globals_.append(local if parent < 0 else local @ globals_[parent])

    geometry = dff.geometries[0]
    skin = geometry.skin
    if skin is None:
        raise SystemExit("El modelo no tiene skin")

    frame_by_node_index = [None] * skin.num_bones
    hierarchy = dff.frames[1].hanim
    if hierarchy is None:
        raise SystemExit("El modelo no tiene jerarquía HAnim")
    for bone in hierarchy.bones:
        frame_by_node_index[bone.node_index] = next(
            index
            for index, frame in enumerate(dff.frames)
            if frame.hanim is not None and frame.hanim.node_id == bone.node_id
        )

    skin_matrices: list[np.ndarray] = []
    for bone_index, frame_index in enumerate(frame_by_node_index):
        inverse_bind = np.array(skin.inverse_matrices[bone_index], dtype=float).reshape(4, 4)
        inverse_bind[3, 3] = 1.0
        skin_matrices.append(inverse_bind @ globals_[frame_index])

    posed_vertices = []
    dominant_bones = []
    for vertex, indices, weights in zip(
        geometry.vertices,
        skin.bone_indices,
        skin.weights,
        strict=True,
    ):
        source = np.array([*vertex, 1.0], dtype=float)
        posed = np.zeros(4)
        for bone_index, weight in zip(indices, weights, strict=True):
            if weight > 0.0:
                posed += weight * (source @ skin_matrices[bone_index])
        posed_vertices.append(posed[:3])
        dominant_bones.append(indices[int(np.argmax(weights))] + 1)
    vertices = np.array(posed_vertices)
    if args.hand_local:
        hand_frame_index = next(
            index for index, bone_id in bone_id_by_frame.items() if bone_id == 2
        )
        inverse_hand = np.linalg.inv(globals_[hand_frame_index])
        homogeneous = np.column_stack((vertices, np.ones(len(vertices))))
        vertices = (homogeneous @ inverse_hand)[:, :3]
    for label, first_bone, last_bone in (
        ("thumb", 3, 5),
        ("index", 6, 8),
        ("middle", 9, 11),
    ):
        selected = vertices[
            [first_bone <= bone <= last_bone for bone in dominant_bones]
        ]
        if len(selected) > 0:
            print(
                label,
                "min",
                np.round(selected.min(axis=0), 4),
                "max",
                np.round(selected.max(axis=0), 4),
            )

    triangles = [vertices[[triangle[0], triangle[1], triangle[2]]] for triangle in geometry.triangles]
    views = ((22, -65), (18, 25), (85, -90), (5, -90))
    panel_width = 600
    panel_height = 500
    image = Image.new("RGB", (panel_width * 2, panel_height * 2), "#17191f")
    draw = ImageDraw.Draw(image)
    for view_index, (elevation, azimuth) in enumerate(views):
        elevation_rad = np.radians(elevation)
        azimuth_rad = np.radians(azimuth)
        camera = np.array(
            [
                np.cos(elevation_rad) * np.cos(azimuth_rad),
                np.cos(elevation_rad) * np.sin(azimuth_rad),
                np.sin(elevation_rad),
            ]
        )
        camera /= np.linalg.norm(camera)
        up_reference = np.array([0.0, 0.0, 1.0])
        right = np.cross(camera, up_reference)
        if np.linalg.norm(right) < 0.001:
            up_reference = np.array([0.0, 1.0, 0.0])
            right = np.cross(camera, up_reference)
        right /= np.linalg.norm(right)
        up = np.cross(right, camera)
        view_matrix = np.column_stack((right, up, camera))
        transformed = vertices @ view_matrix
        extent = transformed[:, :2].max(axis=0) - transformed[:, :2].min(axis=0)
        scale = min((panel_width - 70) / extent[0], (panel_height - 70) / extent[1])
        center = (transformed[:, :2].max(axis=0) + transformed[:, :2].min(axis=0)) * 0.5
        panel_x = (view_index % 2) * panel_width
        panel_y = (view_index // 2) * panel_height

        projected = np.empty((len(vertices), 2))
        projected[:, 0] = panel_x + panel_width * 0.5 + (transformed[:, 0] - center[0]) * scale
        projected[:, 1] = panel_y + panel_height * 0.5 - (transformed[:, 1] - center[1]) * scale
        ordered = sorted(
            enumerate(geometry.triangles),
            key=lambda item: transformed[[item[1][0], item[1][1], item[1][2]], 2].mean(),
        )
        for _, triangle in ordered:
            indices = [triangle[0], triangle[1], triangle[2]]
            points_3d = vertices[indices]
            normal = np.cross(points_3d[1] - points_3d[0], points_3d[2] - points_3d[0])
            normal_length = np.linalg.norm(normal)
            light = 0.55 if normal_length == 0.0 else 0.45 + 0.45 * abs(np.dot(normal / normal_length, camera))
            triangle_bones = [dominant_bones[index] for index in indices]
            if sum(3 <= bone <= 5 for bone in triangle_bones) >= 2:
                base = np.array([239, 106, 74])
            elif sum(6 <= bone <= 8 for bone in triangle_bones) >= 2:
                base = np.array([78, 168, 222])
            else:
                base = np.array([217, 154, 103])
            color = tuple(int(component * light) for component in base)
            polygon = [tuple(projected[index]) for index in indices]
            draw.polygon(polygon, fill=color, outline="#41291c", width=1)

        draw.text(
            (panel_x + 12, panel_y + 10),
            f"elev={elevation} azim={azimuth}",
            fill="white",
        )
    draw.text(
        (12, 980),
        f"{args.model} | {args.animation} | t={args.time:.3f}",
        fill="white",
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
