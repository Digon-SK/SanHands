#!/usr/bin/env python3
"""Extract and validate San Andreas' native skinned gang-hand models."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import shutil
import sys
from pathlib import Path


HAND_MODELS = ("shandl.dff", "shandr.dff", "fhandl.dff", "fhandr.dff")
HAND_ANIMATION_FILE = "ghands.ifp"
POSE_ANIMATION_FILE = "handpose.ifp"
PED_ANIMATION_FILE = "ped.ifp"
EXPECTED_HAND_ANIMATIONS = {
    *(f"LHGsign{index}" for index in range(1, 6)),
    *(f"RHGsign{index}" for index in range(1, 6)),
}
WRIST_BONE_ID = 2
THUMB_BONE_IDS = (3, 4, 5)
MIDDLE_FINGER_BONE_IDS = (9, 10, 11)
DIGIT_BONE_IDS = tuple(range(3, 18))
POSE_CLOSED_FRAME_INDICES = (2, 3)
FUCKU_POSE_FRAME_INDEX = 3
PED_RIGHT_HAND_BONE_ID = 24
FUCKU_WRIST_SAMPLE_TIME = 2.0 / 3.0
FIST_CLOSED_ROTATIONS = {
    "L": {
        3: (0.697021484375, -0.001708984375, 0.625244140625, 0.35107421875),
        4: (0.0068359375, -0.1650390625, -0.082275390625, 0.98291015625),
        5: (-0.009765625, -0.203857421875, 0.224365234375, 0.952880859375),
        6: (0.027587890625, -0.033447265625, 0.64501953125, 0.762939453125),
        7: (0.0, 0.0, 0.83544921875, 0.549560546875),
        8: (0.0, 0.0, 0.764892578125, 0.64404296875),
        9: (0.027587890625, -0.033447265625, 0.645263671875, 0.762451171875),
        10: (0.0, 0.0, 0.835693359375, 0.548828125),
        11: (0.0, 0.0, 0.76513671875, 0.6435546875),
        12: (-0.0185546875, 0.021484375, 0.645751953125, 0.762939453125),
        13: (0.0, 0.0, 0.835693359375, 0.548828125),
        14: (0.0, 0.0, 0.76513671875, 0.6435546875),
        15: (-0.124267578125, 0.063720703125, 0.6259765625, 0.766845703125),
        16: (0.0, 0.0, 0.7529296875, 0.65771484375),
        17: (0.0, 0.0, 0.833984375, 0.551513671875),
    },
    "R": {
        3: (0.705810546875, 0.01416015625, -0.625, -0.3330078125),
        4: (-0.001708984375, 0.16845703125, -0.07470703125, 0.98291015625),
        5: (-0.06689453125, 0.17529296875, 0.24755859375, 0.95068359375),
        6: (0.01123046875, 0.066162109375, 0.642578125, 0.76318359375),
        7: (-0.038330078125, 0.02685546875, 0.8349609375, 0.54833984375),
        8: (0.01513671875, -0.0107421875, 0.764892578125, 0.64404296875),
        9: (0.01123046875, 0.066162109375, 0.642578125, 0.76318359375),
        10: (-0.038330078125, 0.02685546875, 0.835205078125, 0.5478515625),
        11: (0.01513671875, -0.0107421875, 0.764892578125, 0.643798828125),
        12: (0.00732421875, -0.031005859375, 0.645263671875, 0.76318359375),
        13: (0.010986328125, -0.007568359375, 0.835693359375, 0.549072265625),
        14: (-0.00439453125, 0.003173828125, 0.76513671875, 0.64404296875),
        15: (0.006103515625, -0.158203125, 0.60888671875, 0.77734375),
        16: (0.1240234375, -0.0869140625, 0.747802734375, 0.646484375),
        17: (-0.02783203125, 0.01953125, 0.833740234375, 0.55126953125),
    },
}


def object_by_bone(animation: object, bone_id: int) -> object:
    """Return one animation object, failing loudly on an unexpected IFP."""
    objects = [obj for obj in animation.objects if obj.bone_id == bone_id]
    if len(objects) != 1:
        raise ValueError(
            f"{animation.name}: se esperaba un objeto para el hueso {bone_id}"
        )
    return objects[0]


def lock_wrist_to_neutral(animation: object) -> None:
    """Keep the palm aligned with the forearm while finger keys play."""
    wrist = object_by_bone(animation, WRIST_BONE_ID)
    neutral_rotation = wrist.frames[0].rotation
    for frame in wrist.frames:
        frame.rotation = neutral_rotation


def copy_closed_rotations(
    target_animation: object,
    target_bones: tuple[int, ...],
    donor_animation: object,
    donor_bones: tuple[int, ...],
) -> None:
    """Copy only the closed plateau, preserving the target's neutral pose."""
    if len(target_bones) != len(donor_bones):
        raise ValueError("Las cadenas de huesos deben tener el mismo largo")

    for target_bone, donor_bone in zip(target_bones, donor_bones, strict=True):
        target = object_by_bone(target_animation, target_bone)
        donor = object_by_bone(donor_animation, donor_bone)
        if len(donor.frames) <= max(POSE_CLOSED_FRAME_INDICES):
            raise ValueError(
                f"{donor_animation.name}: el hueso {donor_bone} no tiene clave cerrada"
            )
        if len(target.frames) <= max(POSE_CLOSED_FRAME_INDICES):
            neutral_rotation = target.frames[0].rotation
            target.frames = copy.deepcopy(donor.frames)
            for frame in target.frames:
                frame.rotation = neutral_rotation
        for frame_index in POSE_CLOSED_FRAME_INDICES:
            target.frames[frame_index].rotation = donor.frames[frame_index].rotation


def build_fist_animation(source_animation: object, name: str, side: str) -> object:
    """Build an anatomically closed fist while retaining the native timeline."""
    animation = copy.deepcopy(source_animation)
    animation.name = name
    lock_wrist_to_neutral(animation)
    wrist_timeline = object_by_bone(animation, WRIST_BONE_ID).frames
    for bone_id, closed_rotation in FIST_CLOSED_ROTATIONS[side].items():
        target = object_by_bone(animation, bone_id)
        if len(target.frames) <= max(POSE_CLOSED_FRAME_INDICES):
            neutral_rotation = target.frames[0].rotation
            target.frames = copy.deepcopy(wrist_timeline)
            for frame in target.frames:
                frame.rotation = neutral_rotation
        for frame_index in POSE_CLOSED_FRAME_INDICES:
            target.frames[frame_index].rotation = closed_rotation
    return animation


def apply_complete_fist(
    target_animation: object,
    left_fist: object,
    right_fist: object,
) -> None:
    """Replace a native gesture plateau with a complete side-correct fist."""
    donor = left_fist if target_animation.name.startswith("L") else right_fist
    copy_closed_rotations(
        target_animation,
        DIGIT_BONE_IDS,
        donor,
        DIGIT_BONE_IDS,
    )


def apply_right_fucku_pose(
    right_pose: object,
    wrist_rotation: tuple[float, ...],
) -> None:
    """Turn the unused right-hand plateau into a raised-middle-finger fist."""
    wrist = object_by_bone(right_pose, WRIST_BONE_ID)
    wrist.frames[FUCKU_POSE_FRAME_INDEX].rotation = wrist_rotation
    for bone_id in MIDDLE_FINGER_BONE_IDS:
        finger = object_by_bone(right_pose, bone_id)
        finger.frames[FUCKU_POSE_FRAME_INDEX].rotation = finger.frames[0].rotation


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extrae las manos animadas desde gta3.img usando rwfury."
    )
    parser.add_argument("--game-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--rwfury-root", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    sys.path.insert(0, str(args.rwfury_root.resolve()))

    try:
        from rwfury import Dff, Ifp, Img
    except ImportError as exc:
        raise SystemExit(f"No se pudo importar rwfury desde {args.rwfury_root}: {exc}")

    gta3_img = args.game_dir / "models" / "gta3.img"
    anim_img = args.game_dir / "anim" / "anim.img"
    if not gta3_img.is_file():
        raise SystemExit(f"No existe {gta3_img}")
    if not anim_img.is_file():
        raise SystemExit(f"No existe {anim_img}")
    ped_ifp = args.game_dir / "anim" / PED_ANIMATION_FILE
    if not ped_ifp.is_file():
        raise SystemExit(f"No existe {ped_ifp}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    model_archive = Img.from_file(gta3_img)
    manifest: dict[str, object] = {
        "model_source": str(gta3_img),
        "animation_source": str(anim_img),
        "rwfury_root": str(args.rwfury_root.resolve()),
        "models": {},
    }

    for model_name in HAND_MODELS:
        raw = model_archive.read(model_name)
        dff = Dff.from_bytes(raw)
        if len(dff.frames) != 18 or len(dff.geometries) != 1:
            raise SystemExit(
                f"{model_name}: jerarquia inesperada "
                f"({len(dff.frames)} frames, {len(dff.geometries)} geometrias)"
            )

        geometry = dff.geometries[0]
        if geometry.skin is None or geometry.skin.num_bones != 17:
            raise SystemExit(f"{model_name}: no contiene el skin esperado de 17 huesos")
        bone_frames = [frame.name.strip() for frame in dff.frames[1:]]
        if sum("Finger" in name for name in bone_frames) != 15:
            raise SystemExit(f"{model_name}: no contiene los 15 nodos de dedos esperados")

        target = args.output_dir / model_name
        target.write_bytes(raw)
        modified_raw = target.read_bytes()
        modified_check = Dff.from_bytes(modified_raw)
        modified_geometry = modified_check.geometries[0]
        if len(modified_geometry.vertices) != len(geometry.vertices):
            raise SystemExit(f"{model_name}: cambió el número de vértices")
        if modified_geometry.bounding_sphere[3] < geometry.bounding_sphere[3] - 0.001:
            raise SystemExit(f"{model_name}: límites inválidos tras cerrar la muñeca")

        manifest["models"][model_name] = {
            "bytes": len(modified_raw),
            "sha256": hashlib.sha256(modified_raw).hexdigest(),
            "source_sha256": hashlib.sha256(raw).hexdigest(),
            "frames": len(dff.frames),
            "bones": geometry.skin.num_bones,
            "vertices": len(geometry.vertices),
            "triangles": len(geometry.triangles),
            "bone_frames": bone_frames,
            "wrist_join": "runtime-ped-to-hand-snap",
        }

    source_animation_raw = Img.from_file(anim_img).read(HAND_ANIMATION_FILE)
    animation_package = Ifp.from_bytes(source_animation_raw)
    animation_names = {animation.name for animation in animation_package.animations}
    if (
        animation_package.internal_name.upper() != "GHANDS"
        or len(animation_package.animations) != 20
        or not EXPECTED_HAND_ANIMATIONS.issubset(animation_names)
    ):
        raise SystemExit("ghands.ifp: paquete o secuencias de dedos inesperadas")

    # The gang-hand wrist key contains gesture-specific palm inclination. Lock
    # it to the neutral first key while retaining every finger gesture.
    for animation in animation_package.animations:
        if animation.name in EXPECTED_HAND_ANIMATIONS:
            lock_wrist_to_neutral(animation)

    # The native signs deliberately extend individual fingers. Use their open
    # endpoints and timing, but replace the closed plateau with an anatomical
    # pose measured against the actual skinned meshes. Each side has its own
    # rotations because a quaternion sign flip is not a spatial hand mirror.
    left_pose = build_fist_animation(
        animation_package.get_animation("LHGsign1"),
        "LHGrip",
        "L",
    )
    right_pose = build_fist_animation(
        animation_package.get_animation("RHGsign1"),
        "RHGrip",
        "R",
    )

    # Native hand-signal tasks create their own CHandObject instances. Replace
    # their entire gesture plateau, not only the thumb, so short two-key finger
    # tracks and intentionally extended sign fingers cannot reopen the fist.
    for animation_name in EXPECTED_HAND_ANIMATIONS:
        hand_signal = animation_package.get_animation(animation_name)
        apply_complete_fist(hand_signal, left_pose, right_pose)

    # The controller samples frame 2 for a normal fist and frame 3 only while
    # ped.ifp's FUCKU association is visible. All digits remain curled at frame
    # 3 except the three-bone middle-finger chain, which uses its open rotation.
    # Apply this after patching ghands so native gang signs retain full fists.
    ped_package = Ifp.from_bytes(ped_ifp.read_bytes())
    fucku_animation = ped_package.get_animation("FUCKU")
    fucku_wrist = object_by_bone(fucku_animation, PED_RIGHT_HAND_BONE_ID)
    wrist_frame = min(
        fucku_wrist.frames,
        key=lambda frame: abs(frame.time - FUCKU_WRIST_SAMPLE_TIME),
    )
    apply_right_fucku_pose(right_pose, wrist_frame.rotation)

    animation_raw = animation_package.to_bytes()
    animation_check = Ifp.from_bytes(animation_raw)
    for animation_name in EXPECTED_HAND_ANIMATIONS:
        animation = animation_check.get_animation(animation_name)
        wrist = object_by_bone(animation, WRIST_BONE_ID)
        if any(frame.rotation != wrist.frames[0].rotation for frame in wrist.frames):
            raise SystemExit(f"{animation_name}: la muñeca no quedó neutral")
        fist_pose = left_pose if animation_name.startswith("L") else right_pose
        for digit_bone in DIGIT_BONE_IDS:
            actual = object_by_bone(animation, digit_bone).frames[2].rotation
            expected = object_by_bone(fist_pose, digit_bone).frames[2].rotation
            if any(
                abs(component - reference) > 0.001
                for component, reference in zip(actual, expected, strict=True)
            ):
                raise SystemExit(
                    f"{animation_name}: el dedo {digit_bone} no forma el puño"
                )
    (args.output_dir / HAND_ANIMATION_FILE).write_bytes(animation_raw)

    pose_package = Ifp()
    pose_package.internal_name = "SANHANDS"
    pose_package.animations = [left_pose, right_pose]
    pose_raw = pose_package.to_bytes()
    pose_check = Ifp.from_bytes(pose_raw)
    if [animation.name for animation in pose_check.animations] != ["LHGrip", "RHGrip"]:
        raise SystemExit("handpose.ifp: fallo al validar las secuencias clonadas")
    for pose_name in ("LHGrip", "RHGrip"):
        pose = pose_check.get_animation(pose_name)
        wrist = object_by_bone(pose, WRIST_BONE_ID)
        if any(
            frame.rotation != wrist.frames[0].rotation
            for index, frame in enumerate(wrist.frames)
            if not (pose_name == "RHGrip" and index == FUCKU_POSE_FRAME_INDEX)
        ):
            raise SystemExit(f"{pose_name}: la muñeca cambia de inclinación")
        for thumb_bone in THUMB_BONE_IDS:
            thumb = object_by_bone(pose, thumb_bone)
            if thumb.frames[2].rotation == thumb.frames[1].rotation:
                raise SystemExit(f"{pose_name}: el pulgar no se recoge en el puño")
    for pose_name, side in (("LHGrip", "L"), ("RHGrip", "R")):
        pose = pose_check.get_animation(pose_name)
        for bone_id, expected in FIST_CLOSED_ROTATIONS[side].items():
            actual = object_by_bone(pose, bone_id).frames[2].rotation
            if any(
                abs(component - reference) > 0.001
                for component, reference in zip(actual, expected, strict=True)
            ):
                raise SystemExit(f"{pose_name}: pose anatómica inválida en {bone_id}")
    right_pose_check = pose_check.get_animation("RHGrip")
    actual_wrist = object_by_bone(
        right_pose_check,
        WRIST_BONE_ID,
    ).frames[FUCKU_POSE_FRAME_INDEX].rotation
    if any(
        abs(component - reference) > 0.001
        for component, reference in zip(
            actual_wrist,
            wrist_frame.rotation,
            strict=True,
        )
    ):
        raise SystemExit("RHFuckU: la muñeca no conserva la supinación")
    for bone_id in DIGIT_BONE_IDS:
        bone = object_by_bone(right_pose_check, bone_id)
        expected = (
            bone.frames[0].rotation
            if bone_id in MIDDLE_FINGER_BONE_IDS
            else FIST_CLOSED_ROTATIONS["R"][bone_id]
        )
        actual = bone.frames[FUCKU_POSE_FRAME_INDEX].rotation
        if any(
            abs(component - reference) > 0.001
            for component, reference in zip(actual, expected, strict=True)
        ):
            raise SystemExit(f"RHFuckU: pose inválida en el hueso {bone_id}")
    (args.output_dir / POSE_ANIMATION_FILE).write_bytes(pose_raw)

    manifest["animation"] = {
        "file": HAND_ANIMATION_FILE,
        "bytes": len(animation_raw),
        "sha256": hashlib.sha256(animation_raw).hexdigest(),
        "source_sha256": hashlib.sha256(source_animation_raw).hexdigest(),
        "package": animation_package.internal_name,
        "animations": [animation.name for animation in animation_package.animations],
        "hand_sequences": sorted(EXPECTED_HAND_ANIMATIONS),
        "native_signal_pose": "complete side-correct SanHands fist",
    }
    manifest["pose_animation"] = {
        "file": POSE_ANIMATION_FILE,
        "bytes": len(pose_raw),
        "sha256": hashlib.sha256(pose_raw).hexdigest(),
        "package": pose_check.internal_name,
        "animations": [animation.name for animation in pose_check.animations],
        "source": HAND_ANIMATION_FILE,
        "neutral_wrist_bone": f"{WRIST_BONE_ID} outside FUCKU",
        "fist_source": "mesh-measured side-specific anatomical pose",
        "index_pose": "fully flexed beside the middle finger",
        "thumb_pose": "opposed across the curled index and middle fingers",
        "fucku_pose": (
            "right supinated fist with only the middle-finger chain extended"
        ),
    }
    license_source = args.rwfury_root / "LICENSE"
    if license_source.is_file():
        shutil.copy2(license_source, args.output_dir / "LICENSE.rwfury.txt")

    (args.output_dir / "hands-assets.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(
        f"Validados y extraidos {len(HAND_MODELS)} modelos y "
        f"{HAND_ANIMATION_FILE}/{POSE_ANIMATION_FILE} en {args.output_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
