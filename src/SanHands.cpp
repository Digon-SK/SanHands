/*
    SanHands - animated fingers for GTA San Andreas pedestrians.
    Built on plugin-sdk and the game's native CHandObject implementation.
*/

#include "plugin.h"

#include "CAnimBlendAssociation.h"
#include "CAnimBlendHierarchy.h"
#include "CAnimManager.h"
#include "CHandObject.h"
#include "CKeyGen.h"
#include "CModelInfo.h"
#include "CPed.h"
#include "CPedIntelligence.h"
#include "CPedModelInfo.h"
#include "CPopulation.h"
#include "CStreaming.h"
#include "CTaskManager.h"
#include "CTimer.h"
#include "CTxdStore.h"
#include "CWeapon.h"
#include "CWorld.h"
#include "common.h"
#include "eAnimations.h"
#include "eModelID.h"
#include "ePedBones.h"
#include "ePedState.h"
#include "eTaskType.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace san_hands {

namespace {

constexpr std::uintptr_t hand_object_constructor_address{0x59EEB0};
constexpr std::uintptr_t hand_object_render_address{0x59EE80};
constexpr std::uintptr_t hand_object_count_address{0xBB4A70};
constexpr std::uintptr_t entity_render_clump_call_address{0x53439C};
constexpr std::uintptr_t right_weapon_render_call_address{0x733087};
constexpr std::uintptr_t left_weapon_render_call_address{0x733119};
constexpr int anim_streaming_base{0x63E7};
constexpr std::size_t absolute_max_peds{96};
constexpr std::uint16_t native_object_limit{150};
constexpr std::uint32_t stale_entry_milliseconds{1500};
constexpr char animation_block_name[]{"ghands"};
constexpr char pose_animation_block_name[]{"sanhands"};
constexpr char pose_animation_file_name[]{"handpose.ifp"};
constexpr char hand_texture_dictionary_file_name[]{"GangHands.txd"};
constexpr char hand_texture_dictionary_slot_name[]{"sanhands_textures"};
constexpr char dark_hand_texture_name[]{"hands_black"};
constexpr char light_hand_texture_name[]{"hands_white"};
constexpr char left_grip_animation_name[]{"LHGrip"};
constexpr char right_grip_animation_name[]{"RHGrip"};
constexpr float fist_pose_time{0.6666667F};
constexpr float fucku_pose_time{1.3333333F};
constexpr float rest_pose_time{0.56F};
constexpr float minimum_visible_animation_blend{0.01F};
constexpr int articulated_hand_bone_id{2};
constexpr int left_wrist_bone_id{BONE_LEFTWRIST};
constexpr int right_wrist_bone_id{BONE_RIGHTWRIST};
constexpr int left_hand_bone_id{BONE_LEFTHAND};
constexpr int right_hand_bone_id{BONE_RIGHTHAND};
constexpr char ini_file_name[]{"SanHands.ini"};
constexpr char log_file_name[]{"SanHands.log"};
constexpr std::size_t max_cuff_candidates{256};
constexpr std::size_t max_loop_points{64};
constexpr std::size_t max_ped_geometry_vertices{8192};
constexpr std::size_t max_ped_geometry_triangles{16384};
constexpr std::size_t max_ped_geometry_overrides{8};
constexpr float point_merge_distance_squared{0.000001F};
constexpr float minimum_forearm_weight{0.05F};
constexpr float fallback_cuff_depth{0.055F};
constexpr float two_pi{6.2831853071795864769F};

struct LoopPoint {
    RwV3d position{};
    RwTexCoords uv{};
    float angle{0.0F};
    int source_index{-1};
};

struct CuffExtractionContext {
    int forearm_index{-1};
    int hidden_subtree_end{-1};
    std::array<RwV3d, max_cuff_candidates> boundary_points{};
    std::array<RwV3d, max_cuff_candidates> fallback_points{};
    std::size_t boundary_count{0};
    std::size_t fallback_count{0};
};

struct PedGeometryOverride {
    RpAtomic* atomic{nullptr};
    RpGeometry* original_geometry{nullptr};
};

struct PedGeometryOverrideSet {
    RpClump* clump{nullptr};
    std::array<PedGeometryOverride, max_ped_geometry_overrides> items{};
    std::size_t count{0};
};

struct ForearmSnapTarget {
    int forearm_index{-1};
    const std::array<LoopPoint, max_loop_points>* cuff_loop{nullptr};
    std::size_t cuff_count{0};
    const std::array<LoopPoint, max_loop_points>* hand_loop{nullptr};
    std::size_t hand_count{0};
    bool modified{false};
};

struct PedSnapContext {
    std::array<ForearmSnapTarget, 2> forearms{};
    PedGeometryOverrideSet* overrides{nullptr};
    bool failed{false};
};

struct Settings {
    bool enabled{true};
    bool enable_player{true};
    bool enable_npcs{true};
    int max_peds{56};
    float max_distance{110.0F};
    float grip_transition_speed{0.12F};
    float fucku_transition_speed{0.08F};
    bool inertia3d_compatibility{true};
};

struct PedHands {
    CPed* ped{nullptr};
    CHandObject* left{nullptr};
    CHandObject* right{nullptr};
    CAnimBlendAssociation* left_animation{nullptr};
    CAnimBlendAssociation* right_animation{nullptr};
    RwMatrix inertia_left_wrist{};
    RwMatrix inertia_right_wrist{};
    RwMatrix left_weapon_offset{};
    RwMatrix right_weapon_offset{};
    RwMatrix left_weapon_anchor{};
    RwMatrix right_weapon_anchor{};
    RwMatrix fucku_right_wrist{};
    PedGeometryOverrideSet geometry_overrides{};
    float grip{0.0F};
    float fucku_blend{0.0F};
    std::uint32_t last_seen{0};
    std::uint32_t inertia_capture_frame{0};
    std::uint32_t left_weapon_anchor_frame{0};
    std::uint32_t right_weapon_anchor_frame{0};
    std::uint32_t fucku_capture_frame{0};
    bool inertia_wrist_pose_valid{false};
    bool left_weapon_offset_valid{false};
    bool right_weapon_offset_valid{false};
    bool left_weapon_anchor_valid{false};
    bool right_weapon_anchor_valid{false};
    bool fucku_wrist_pose_valid{false};
};

[[nodiscard]] std::uint16_t& native_object_count() noexcept {
    return *reinterpret_cast<std::uint16_t*>(hand_object_count_address);
}

[[nodiscard]] int read_int(
    const char* const path,
    const char* const key,
    const int fallback) noexcept {
    return GetPrivateProfileIntA("SanHands", key, fallback, path);
}

[[nodiscard]] float read_float(
    const char* const path,
    const char* const key,
    const float fallback) noexcept {
    std::array<char, 64> fallback_text{};
    std::array<char, 64> value_text{};
    std::snprintf(fallback_text.data(), fallback_text.size(), "%.3f", fallback);
    GetPrivateProfileStringA(
        "SanHands",
        key,
        fallback_text.data(),
        value_text.data(),
        static_cast<DWORD>(value_text.size()),
        path);

    char* parse_end{nullptr};
    const float parsed{std::strtof(value_text.data(), &parse_end)};
    return parse_end != value_text.data() ? parsed : fallback;
}

[[nodiscard]] bool is_fat_hand_model(const CPed& ped) noexcept {
    constexpr std::array<short, 3> fat_hand_ped_models{103, 105, 107};
    return std::find(
               fat_hand_ped_models.begin(),
               fat_hand_ped_models.end(),
               ped.m_nModelIndex) != fat_hand_ped_models.end();
}

[[nodiscard]] bool has_native_hand_signal(const CPed& ped) noexcept {
    if (ped.m_pIntelligence == nullptr) {
        return false;
    }

    auto& tasks{ped.m_pIntelligence->m_TaskMgr};
    return tasks.FindTaskByType(
               TASK_SECONDARY_PARTIAL_ANIM,
               TASK_COMPLEX_HANDSIGNAL_ANIM) != nullptr ||
           tasks.FindTaskByType(
               TASK_SECONDARY_PARTIAL_ANIM,
               TASK_SIMPLE_HANDSIGNAL_ANIM) != nullptr;
}

[[nodiscard]] float squared_distance(const CPed& lhs, const CPed& rhs) noexcept {
    const CVector delta{lhs.GetPosition() - rhs.GetPosition()};
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}

[[nodiscard]] bool has_valid_basis(const RwMatrix& matrix) noexcept {
    const auto length_squared{[](const RwV3d& axis) noexcept {
        return axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    }};
    constexpr float minimum_basis_length_squared{0.01F};
    return length_squared(matrix.right) > minimum_basis_length_squared &&
           length_squared(matrix.up) > minimum_basis_length_squared &&
           length_squared(matrix.at) > minimum_basis_length_squared;
}

[[nodiscard]] constexpr float smoothstep(const float value) noexcept {
    const float clamped{std::clamp(value, 0.0F, 1.0F)};
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

[[nodiscard]] RwMatrix blend_rigid_transforms(
    const RwMatrix& from,
    const RwMatrix& to,
    const float weight) noexcept {
    const float clamped_weight{std::clamp(weight, 0.0F, 1.0F)};
    RtQuat from_rotation{};
    RtQuat to_rotation{};
    if (RtQuatConvertFromMatrix(&from_rotation, &from) == FALSE ||
        RtQuatConvertFromMatrix(&to_rotation, &to) == FALSE) {
        return clamped_weight < 0.5F ? from : to;
    }

    const float dot{
        from_rotation.imag.x * to_rotation.imag.x +
        from_rotation.imag.y * to_rotation.imag.y +
        from_rotation.imag.z * to_rotation.imag.z +
        from_rotation.real * to_rotation.real};
    const float target_sign{dot < 0.0F ? -1.0F : 1.0F};
    RtQuat blended_rotation{
        {
            from_rotation.imag.x * (1.0F - clamped_weight) +
                to_rotation.imag.x * clamped_weight * target_sign,
            from_rotation.imag.y * (1.0F - clamped_weight) +
                to_rotation.imag.y * clamped_weight * target_sign,
            from_rotation.imag.z * (1.0F - clamped_weight) +
                to_rotation.imag.z * clamped_weight * target_sign,
        },
        from_rotation.real * (1.0F - clamped_weight) +
            to_rotation.real * clamped_weight * target_sign,
    };
    const float rotation_length{std::sqrt(
        blended_rotation.imag.x * blended_rotation.imag.x +
        blended_rotation.imag.y * blended_rotation.imag.y +
        blended_rotation.imag.z * blended_rotation.imag.z +
        blended_rotation.real * blended_rotation.real)};
    if (rotation_length > 0.0001F) {
        const float inverse_length{1.0F / rotation_length};
        blended_rotation.imag.x *= inverse_length;
        blended_rotation.imag.y *= inverse_length;
        blended_rotation.imag.z *= inverse_length;
        blended_rotation.real *= inverse_length;
    }

    RwMatrix result{};
    RtQuatUnitConvertToMatrix(&blended_rotation, &result);
    result.pos = {
        from.pos.x + (to.pos.x - from.pos.x) * clamped_weight,
        from.pos.y + (to.pos.y - from.pos.y) * clamped_weight,
        from.pos.z + (to.pos.z - from.pos.z) * clamped_weight,
    };
    return result;
}

[[nodiscard]] constexpr float weight_at(
    const RwMatrixWeights& weights,
    const int slot) noexcept {
    switch (slot) {
    case 0:
        return weights.w0;
    case 1:
        return weights.w1;
    case 2:
        return weights.w2;
    default:
        return weights.w3;
    }
}

[[nodiscard]] constexpr int bone_at(
    const RwUInt32 packed_indices,
    const int slot) noexcept {
    return static_cast<int>((packed_indices >> (slot * 8)) & 0xFFU);
}

[[nodiscard]] int dominant_bone(
    const RwUInt32 packed_indices,
    const RwMatrixWeights& weights) noexcept {
    int dominant_slot{0};
    for (int slot{1}; slot < 4; ++slot) {
        if (weight_at(weights, slot) > weight_at(weights, dominant_slot)) {
            dominant_slot = slot;
        }
    }
    return bone_at(packed_indices, dominant_slot);
}

[[nodiscard]] float weight_for_bone(
    const RwUInt32 packed_indices,
    const RwMatrixWeights& weights,
    const int bone) noexcept {
    float result{0.0F};
    for (int slot{0}; slot < 4; ++slot) {
        if (bone_at(packed_indices, slot) == bone) {
            result += weight_at(weights, slot);
        }
    }
    return result;
}

void append_unique_point(
    std::array<RwV3d, max_cuff_candidates>& points,
    std::size_t& count,
    const RwV3d& candidate) noexcept {
    for (std::size_t index{0}; index < count; ++index) {
        const RwV3d delta{
            points[index].x - candidate.x,
            points[index].y - candidate.y,
            points[index].z - candidate.z,
        };
        if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <=
            point_merge_distance_squared) {
            return;
        }
    }
    if (count < points.size()) {
        points[count++] = candidate;
    }
}

RpAtomic* collect_cuff_vertices(RpAtomic* const atomic, void* const data) noexcept {
    auto& context{*static_cast<CuffExtractionContext*>(data)};
    RpGeometry* const geometry{RpAtomicGetGeometry(atomic)};
    RpSkin* const skin{
        geometry != nullptr ? RpSkinGeometryGetSkin(geometry) : nullptr};
    if (geometry == nullptr || skin == nullptr || context.forearm_index < 0 ||
        static_cast<RwUInt32>(context.forearm_index) >= RpSkinGetNumBones(skin)) {
        return atomic;
    }

    RpMorphTarget* const morph_target{RpGeometryGetMorphTarget(geometry, 0)};
    const RwV3d* const vertices{
        morph_target != nullptr ? RpMorphTargetGetVertices(morph_target) : nullptr};
    const RwUInt32* const bone_indices{RpSkinGetVertexBoneIndices(skin)};
    const RwMatrixWeights* const weights{RpSkinGetVertexBoneWeights(skin)};
    const RwMatrix* const inverse_matrices{RpSkinGetSkinToBoneMatrices(skin)};
    if (vertices == nullptr || bone_indices == nullptr || weights == nullptr ||
        inverse_matrices == nullptr) {
        return atomic;
    }

    const int vertex_count{RpGeometryGetNumVertices(geometry)};
    const auto to_forearm_space{[&](const int vertex_index) noexcept {
        RwV3d local{};
        RwV3dTransformPoints(
            &local,
            &vertices[vertex_index],
            1,
            &inverse_matrices[context.forearm_index]);
        return local;
    }};

    for (int vertex_index{0}; vertex_index < vertex_count; ++vertex_index) {
        if (weight_for_bone(
                bone_indices[vertex_index],
                weights[vertex_index],
                context.forearm_index) >= minimum_forearm_weight) {
            append_unique_point(
                context.fallback_points,
                context.fallback_count,
                to_forearm_space(vertex_index));
        }
    }

    const RpTriangle* const triangles{RpGeometryGetTriangles(geometry)};
    const int triangle_count{RpGeometryGetNumTriangles(geometry)};
    for (int triangle_index{0}; triangle_index < triangle_count; ++triangle_index) {
        const RpTriangle& triangle{triangles[triangle_index]};
        bool crosses_hidden_subtree{false};
        for (const RwUInt16 vertex_index : triangle.vertIndex) {
            const int bone{dominant_bone(
                bone_indices[vertex_index],
                weights[vertex_index])};
            if (bone > context.forearm_index &&
                bone < context.hidden_subtree_end) {
                crosses_hidden_subtree = true;
                break;
            }
        }
        if (!crosses_hidden_subtree) {
            continue;
        }

        for (const RwUInt16 vertex_index : triangle.vertIndex) {
            if (weight_for_bone(
                    bone_indices[vertex_index],
                    weights[vertex_index],
                    context.forearm_index) >= minimum_forearm_weight) {
                append_unique_point(
                    context.boundary_points,
                    context.boundary_count,
                    to_forearm_space(vertex_index));
            }
        }
    }
    return atomic;
}

[[nodiscard]] constexpr float cuff_cross(
    const RwV3d& origin,
    const RwV3d& lhs,
    const RwV3d& rhs) noexcept {
    return (lhs.y - origin.y) * (rhs.z - origin.z) -
           (lhs.z - origin.z) * (rhs.y - origin.y);
}

[[nodiscard]] std::size_t make_cuff_hull(
    const std::array<RwV3d, max_cuff_candidates>& input,
    const std::size_t input_count,
    std::array<LoopPoint, max_loop_points>& output) noexcept {
    if (input_count < 3) {
        return 0;
    }

    std::array<RwV3d, max_cuff_candidates> sorted{input};
    std::sort(
        sorted.begin(),
        sorted.begin() + static_cast<std::ptrdiff_t>(input_count),
        [](const RwV3d& lhs, const RwV3d& rhs) noexcept {
            return lhs.y < rhs.y || (lhs.y == rhs.y && lhs.z < rhs.z);
        });

    std::size_t unique_count{0};
    for (std::size_t index{0}; index < input_count; ++index) {
        if (unique_count == 0 ||
            std::abs(sorted[index].y - sorted[unique_count - 1].y) > 0.001F ||
            std::abs(sorted[index].z - sorted[unique_count - 1].z) > 0.001F) {
            sorted[unique_count++] = sorted[index];
        }
    }
    if (unique_count < 3) {
        return 0;
    }

    std::array<RwV3d, max_cuff_candidates * 2> hull{};
    std::size_t hull_count{0};
    for (std::size_t index{0}; index < unique_count; ++index) {
        while (hull_count >= 2 &&
               cuff_cross(hull[hull_count - 2], hull[hull_count - 1], sorted[index]) <=
                   0.0F) {
            --hull_count;
        }
        hull[hull_count++] = sorted[index];
    }
    const std::size_t lower_count{hull_count};
    for (std::size_t index{unique_count - 1}; index-- > 0;) {
        while (hull_count > lower_count &&
               cuff_cross(hull[hull_count - 2], hull[hull_count - 1], sorted[index]) <=
                   0.0F) {
            --hull_count;
        }
        hull[hull_count++] = sorted[index];
    }
    if (hull_count > 1) {
        --hull_count;
    }
    if (hull_count < 3 || hull_count > output.size()) {
        return 0;
    }

    for (std::size_t index{0}; index < hull_count; ++index) {
        output[index].position = hull[index];
    }
    return hull_count;
}

void sort_loop_by_angle(
    std::array<LoopPoint, max_loop_points>& loop,
    const std::size_t count) noexcept {
    float center_y{0.0F};
    float center_z{0.0F};
    for (std::size_t index{0}; index < count; ++index) {
        center_y += loop[index].position.y;
        center_z += loop[index].position.z;
    }
    center_y /= static_cast<float>(count);
    center_z /= static_cast<float>(count);
    for (std::size_t index{0}; index < count; ++index) {
        float angle{std::atan2(
            loop[index].position.z - center_z,
            loop[index].position.y - center_y)};
        if (angle < 0.0F) {
            angle += two_pi;
        }
        loop[index].angle = angle;
    }
    std::sort(
        loop.begin(),
        loop.begin() + static_cast<std::ptrdiff_t>(count),
        [](const LoopPoint& lhs, const LoopPoint& rhs) noexcept {
            return lhs.angle < rhs.angle;
        });
}

[[nodiscard]] RwV3d sample_loop(
    const std::array<LoopPoint, max_loop_points>& loop,
    const std::size_t count,
    const float angle) noexcept {
    for (std::size_t upper{0}; upper < count; ++upper) {
        const std::size_t lower{upper == 0 ? count - 1 : upper - 1};
        const float lower_angle{
            upper == 0 ? loop[lower].angle - two_pi : loop[lower].angle};
        const float upper_angle{loop[upper].angle};
        const float adjusted_angle{
            upper == 0 && angle > upper_angle ? angle - two_pi : angle};
        if (adjusted_angle < lower_angle || adjusted_angle > upper_angle) {
            continue;
        }

        const float span{upper_angle - lower_angle};
        const float weight{
            span > 0.000001F
                ? (adjusted_angle - lower_angle) / span
                : 0.0F};
        return {
            loop[lower].position.x +
                (loop[upper].position.x - loop[lower].position.x) * weight,
            loop[lower].position.y +
                (loop[upper].position.y - loop[lower].position.y) * weight,
            loop[lower].position.z +
                (loop[upper].position.z - loop[lower].position.z) * weight,
        };
    }
    return loop[0].position;
}

} // namespace

class SanHandsMod final {
public:
    SanHandsMod() {
        active_instance_ = this;
        resolve_module_paths();
        load_settings();
        log("SanHands cargado; esperando la inicializacion del juego.");

        plugin::Events::initGameEvent += [this] {
            resources_requested_ = false;
            resources_ready_ = false;
            load_settings();
        };
        plugin::Events::gameProcessEvent += [this] { on_game_process(); };
        plugin::Events::pedRenderEvent.before += [this](CPed* const ped) {
            on_ped_render(ped);
        };
        plugin::Events::objectRenderEvent.before += [this](CObject* const object) {
            align_hand_with_inertia_pose(object);
        };
        plugin::Events::pedSetModelEvent.after += [this](CPed* const ped, int) {
            remove_for_ped(ped);
        };
        plugin::Events::pedDtorEvent.before += [this](CPed* const ped) {
            remove_for_ped(ped);
        };
        plugin::Events::shutdownRwEvent += [this] {
            release_all();
            unload_hand_textures();
        };
        plugin::Events::shutdownPoolsEvent += [this] {
            entries_.fill({});
            active_peds_ = 0;
            pose_anim_block_index_ = -1;
            resources_requested_ = false;
            resources_ready_ = false;
            inertia3d_detected_ = false;
            inertia3d_detection_logged_ = false;
            hand_txd_slot_ = -1;
            dark_hand_texture_ = nullptr;
            light_hand_texture_ = nullptr;
        };
    }

private:
    void resolve_module_paths() noexcept {
        HMODULE module{nullptr};
        const auto address{reinterpret_cast<LPCSTR>(this)};
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                address,
                &module) == FALSE) {
            std::strcpy(ini_path_.data(), ini_file_name);
            std::strcpy(log_path_.data(), log_file_name);
            std::strcpy(pose_animation_path_.data(), pose_animation_file_name);
            std::strcpy(
                hand_texture_dictionary_path_.data(),
                hand_texture_dictionary_file_name);
            return;
        }

        std::array<char, MAX_PATH> module_path{};
        GetModuleFileNameA(module, module_path.data(), static_cast<DWORD>(module_path.size()));
        char* const separator{std::strrchr(module_path.data(), '\\')};
        if (separator != nullptr) {
            separator[1] = '\0';
        } else {
            module_path[0] = '\0';
        }

        std::snprintf(ini_path_.data(), ini_path_.size(), "%s%s", module_path.data(), ini_file_name);
        std::snprintf(log_path_.data(), log_path_.size(), "%s%s", module_path.data(), log_file_name);
        std::snprintf(
            pose_animation_path_.data(),
            pose_animation_path_.size(),
            "%s%s",
            module_path.data(),
            pose_animation_file_name);
        std::snprintf(
            hand_texture_dictionary_path_.data(),
            hand_texture_dictionary_path_.size(),
            "%s%s",
            module_path.data(),
            hand_texture_dictionary_file_name);
    }

    void load_settings() noexcept {
        Settings loaded{};
        loaded.enabled = read_int(ini_path_.data(), "Enabled", 1) != 0;
        loaded.enable_player = read_int(ini_path_.data(), "Player", 1) != 0;
        loaded.enable_npcs = read_int(ini_path_.data(), "NPCs", 1) != 0;
        loaded.max_peds = std::clamp(read_int(ini_path_.data(), "MaxPeds", 56), 1, 96);
        loaded.max_distance = std::clamp(
            read_float(ini_path_.data(), "MaxDistance", 110.0F), 20.0F, 400.0F);
        loaded.grip_transition_speed = std::clamp(
            read_float(ini_path_.data(), "GripTransitionSpeed", 0.12F), 0.01F, 1.0F);
        loaded.fucku_transition_speed = std::clamp(
            read_float(ini_path_.data(), "FuckUTransitionSpeed", 0.08F),
            0.01F,
            1.0F);
        loaded.inertia3d_compatibility =
            read_int(ini_path_.data(), "Inertia3DCompatibility", 1) != 0;
        settings_ = loaded;
    }

    void log(const char* const message) const noexcept {
        const HANDLE file{CreateFileA(
            log_path_.data(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr)};
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD written{0};
        WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
        constexpr char newline[]{"\r\n"};
        WriteFile(file, newline, 2, &written, nullptr);
        CloseHandle(file);
    }

    void request_resources() noexcept {
        if (resources_requested_) {
            return;
        }

        resources_requested_ = true;
        anim_block_index_ = CAnimManager::GetAnimationBlockIndex(animation_block_name);
        if (anim_block_index_ < 0) {
            log("ERROR: no se encontro el bloque de animacion ghands.");
            return;
        }
        if (!load_pose_animations()) {
            return;
        }
        if (!load_hand_textures()) {
            return;
        }
        constexpr std::array<int, 4> hand_models{
            MODEL_SHANDL,
            MODEL_SHANDR,
            MODEL_FHANDL,
            MODEL_FHANDR,
        };
        for (const int model : hand_models) {
            CStreaming::RequestModel(model, KEEP_IN_MEMORY);
        }
        CStreaming::RequestModel(anim_streaming_base + anim_block_index_, KEEP_IN_MEMORY);
        CStreaming::LoadAllRequestedModels(false);

        resources_ready_ = std::all_of(
            hand_models.begin(), hand_models.end(), CStreaming::HasModelLoaded) &&
            CStreaming::HasModelLoaded(anim_streaming_base + anim_block_index_);
        log(resources_ready_
                ? "Modelos de manos y animaciones de dedos cargados correctamente."
                : "ERROR: no fue posible cargar todos los recursos de manos.");
    }

    [[nodiscard]] bool load_hand_textures() noexcept {
        if (dark_hand_texture_ != nullptr && light_hand_texture_ != nullptr) {
            return true;
        }

        unload_hand_textures();
        const int slot{CTxdStore::AddTxdSlot(hand_texture_dictionary_slot_name)};
        if (slot < 0) {
            log("ERROR: no se pudo reservar el diccionario GangHands.txd.");
            return false;
        }
        if (!CTxdStore::LoadTxd(slot, hand_texture_dictionary_path_.data())) {
            CTxdStore::RemoveTxdSlot(slot);
            log("ERROR: no se pudo cargar GangHands.txd.");
            return false;
        }

        TxdDef* const definition{CTxdStore::ms_pTxdPool->GetAt(slot)};
        RwTexDictionary* const dictionary{
            definition != nullptr ? definition->m_pRwDictionary : nullptr};
        if (dictionary == nullptr) {
            CTxdStore::RemoveTxdSlot(slot);
            log("ERROR: GangHands.txd no contiene un diccionario valido.");
            return false;
        }

        RwTexture* const dark{RwTexDictionaryFindHashNamedTexture(
            dictionary,
            CKeyGen::GetUppercaseKey(dark_hand_texture_name))};
        RwTexture* const light{RwTexDictionaryFindHashNamedTexture(
            dictionary,
            CKeyGen::GetUppercaseKey(light_hand_texture_name))};
        if (dark == nullptr || light == nullptr) {
            CTxdStore::RemoveTxdSlot(slot);
            log("ERROR: faltan hands_black o hands_white en GangHands.txd.");
            return false;
        }

        hand_txd_slot_ = slot;
        dark_hand_texture_ = dark;
        light_hand_texture_ = light;
        log("GangHands.txd cargado: seleccion de textura por raza activada.");
        return true;
    }

    void unload_hand_textures() noexcept {
        dark_hand_texture_ = nullptr;
        light_hand_texture_ = nullptr;
        if (hand_txd_slot_ >= 0) {
            CTxdStore::RemoveTxdSlot(hand_txd_slot_);
            hand_txd_slot_ = -1;
        }
    }

    [[nodiscard]] RwTexture* select_hand_texture(const CPed& ped) const noexcept {
        const auto* const model_info{
            static_cast<const CPedModelInfo*>(CModelInfo::GetModelInfo(ped.m_nModelIndex))};
        return model_info != nullptr && model_info->m_nRace == RACE_BLACK
                   ? dark_hand_texture_
                   : light_hand_texture_;
    }

    [[nodiscard]] bool load_pose_animations() noexcept {
        pose_anim_block_index_ =
            CAnimManager::GetAnimationBlockIndex(pose_animation_block_name);
        if (pose_anim_block_index_ >= 0) {
            return true;
        }

        RwStream* const stream{RwStreamOpen(
            rwSTREAMFILENAME,
            rwSTREAMREAD,
            pose_animation_path_.data())};
        if (stream == nullptr) {
            log("ERROR: no se pudo abrir handpose.ifp.");
            return false;
        }

        CAnimManager::LoadAnimFile(stream, true, nullptr);
        RwStreamClose(stream, nullptr);
        pose_anim_block_index_ =
            CAnimManager::GetAnimationBlockIndex(pose_animation_block_name);
        if (pose_anim_block_index_ < 0) {
            log("ERROR: handpose.ifp no registro el bloque SANHANDS.");
            return false;
        }
        return true;
    }

    void on_game_process() noexcept {
        if (!settings_.enabled) {
            release_all();
            return;
        }

        install_weapon_render_hooks();
        detect_inertia3d();
        request_resources();
        if (!resources_ready_) {
            return;
        }

        const std::uint32_t now{CTimer::m_snTimeInMilliseconds};
        for (auto& entry : entries_) {
            if (entry.ped != nullptr) {
                const bool native_signal{has_native_hand_signal(*entry.ped)};
                const bool render_with_ragdoll{
                    inertia3d_detected_ && is_inertia_driven(*entry.ped)};
                const bool fucku_active{is_playing_fucku(*entry.ped)};
                set_entry_visible(
                    entry,
                    !native_signal && !render_with_ragdoll);
                update_finger_pose(entry, fucku_active);
                capture_fucku_wrist(entry, entry.fucku_blend > 0.0F);
            }
        }

        if (now - last_cleanup_ < 250U) {
            return;
        }
        last_cleanup_ = now;

        for (auto& entry : entries_) {
            if (entry.ped != nullptr && now - entry.last_seen > stale_entry_milliseconds) {
                destroy_entry(entry);
            }
        }
    }

    void detect_inertia3d() noexcept {
        if (!settings_.inertia3d_compatibility || inertia3d_detected_) {
            return;
        }

        inertia3d_detected_ =
            GetModuleHandleA("InertiaBox.asi") != nullptr ||
            GetModuleHandleA("InertiaBox.dll") != nullptr;
        if (inertia3d_detected_) {
            install_clump_render_hook();
            if (!inertia3d_detection_logged_) {
                log("Inertia3D detectado; seguimiento fisico de munecas activado.");
                inertia3d_detection_logged_ = true;
            }
        }
    }

    using ClumpRenderFunction = RpClump*(__cdecl*)(RpClump*);

    void install_weapon_render_hooks() noexcept {
        if (weapon_render_hooks_registered_) {
            return;
        }

        const auto previous_right{injector::MakeCALL(
            right_weapon_render_call_address,
            injector::raw_ptr(&render_right_weapon_with_articulated_anchor))};
        const auto previous_left{injector::MakeCALL(
            left_weapon_render_call_address,
            injector::raw_ptr(&render_left_weapon_with_articulated_anchor))};
        original_right_weapon_render_ = previous_right.get();
        original_left_weapon_render_ = previous_left.get();
        weapon_render_hooks_registered_ =
            original_right_weapon_render_ != nullptr &&
            original_left_weapon_render_ != nullptr;
        log(weapon_render_hooks_registered_
                ? "Anclaje de armas a las manos articuladas registrado correctamente."
                : "ERROR: no se pudieron registrar los hooks de render de armas.");
    }

    static RpClump* __cdecl render_right_weapon_with_articulated_anchor(
        RpClump* const clump) noexcept {
        if (active_instance_ != nullptr) {
            active_instance_->prepare_weapon_clump_for_render(clump, false);
        }
        return original_right_weapon_render_ != nullptr
                   ? original_right_weapon_render_(clump)
                   : clump;
    }

    static RpClump* __cdecl render_left_weapon_with_articulated_anchor(
        RpClump* const clump) noexcept {
        if (active_instance_ != nullptr) {
            active_instance_->prepare_weapon_clump_for_render(clump, true);
        }
        return original_left_weapon_render_ != nullptr
                   ? original_left_weapon_render_(clump)
                   : clump;
    }

    void prepare_weapon_clump_for_render(
        RpClump* const weapon_clump,
        const bool left) noexcept {
        if (weapon_clump == nullptr) {
            return;
        }

        for (auto& entry : entries_) {
            if (entry.ped == nullptr ||
                reinterpret_cast<RpClump*>(entry.ped->m_pWeaponObject) !=
                    weapon_clump) {
                continue;
            }

            const CWeapon* const weapon{entry.ped->GetWeapon()};
            if (weapon == nullptr ||
                weapon->m_eWeaponType == WEAPONTYPE_PARACHUTE) {
                return;
            }

            refresh_weapon_anchor_for_render(entry, left);
            const bool anchor_valid{
                left ? entry.left_weapon_anchor_valid
                     : entry.right_weapon_anchor_valid};
            const std::uint32_t anchor_frame{
                left ? entry.left_weapon_anchor_frame
                     : entry.right_weapon_anchor_frame};
            if (!anchor_valid || CTimer::m_FrameCounter - anchor_frame > 1U) {
                return;
            }

            const RwMatrix& anchor{
                left ? entry.left_weapon_anchor : entry.right_weapon_anchor};
            RwFrame* const weapon_frame{RpClumpGetFrame(weapon_clump)};
            if (weapon_frame == nullptr) {
                return;
            }
            RwMatrix* const weapon_matrix{RwFrameGetMatrix(weapon_frame)};
            RwMatrixCopy(weapon_matrix, &anchor);

            if (left) {
                const RwV3d x_axis{1.0F, 0.0F, 0.0F};
                const RwV3d twin_weapon_offset{0.04F, -0.05F, 0.0F};
                RwMatrixRotate(
                    weapon_matrix,
                    &x_axis,
                    180.0F,
                    rwCOMBINEPRECONCAT);
                RwMatrixTranslate(
                    weapon_matrix,
                    &twin_weapon_offset,
                    rwCOMBINEPRECONCAT);
            }
            RwFrameUpdateObjects(weapon_frame);
            return;
        }
    }

    static void refresh_weapon_anchor_for_render(
        PedHands& entry,
        const bool left) noexcept {
        CHandObject* const hand{left ? entry.left : entry.right};
        if (hand == nullptr || hand->m_pRwClump == nullptr) {
            return;
        }

        // The deferred weapon pass can happen before objectRenderEvent for the
        // hand. Re-evaluate the articulated hierarchy here instead of relying
        // on a matrix cached by an earlier render phase.
        if (entry.inertia_wrist_pose_valid &&
            CTimer::m_FrameCounter - entry.inertia_capture_frame <= 1U) {
            align_articulated_hand(
                hand,
                left ? entry.inertia_left_wrist
                     : entry.inertia_right_wrist);
        } else {
            hand->UpdateRwFrame();
            hand->UpdateRpHAnim();
        }
        capture_weapon_anchor(entry, left);
    }

    void install_clump_render_hook() noexcept {
        if (clump_render_hook_registered_) {
            return;
        }

        // Hook the actual RpClumpRender call inside CEntity::Render, below every
        // pedRenderEvent callback. This ordering is deterministic even when the
        // ASI loader chains SanHands and Inertia3D in the opposite order.
        const auto previous{injector::MakeCALL(
            entity_render_clump_call_address,
            injector::raw_ptr(&render_clump_with_hidden_original_hands))};
        original_clump_render_ = previous.get();
        clump_render_hook_registered_ = original_clump_render_ != nullptr;
        log(clump_render_hook_registered_
                ? "Hook final de RpClumpRender para Inertia3D registrado correctamente."
                : "ERROR: no se pudo registrar el hook final de RpClumpRender.");
    }

    static RpClump* __cdecl render_clump_with_hidden_original_hands(
        RpClump* const clump) noexcept {
        PedHands* const entry{
            active_instance_ != nullptr
                ? active_instance_->prepare_inertia_hands_for_clump(clump)
                : nullptr};
        RpClump* const result{original_clump_render_ != nullptr
                                  ? original_clump_render_(clump)
                                  : clump};
        if (active_instance_ != nullptr && entry != nullptr) {
            active_instance_->render_inertia_hands(*entry);
        }
        return result;
    }

    [[nodiscard]] PedHands* prepare_inertia_hands_for_clump(
        RpClump* const clump) noexcept {
        if (clump == nullptr) {
            return nullptr;
        }
        for (auto& entry : entries_) {
            if (entry.ped != nullptr && entry.ped->m_pRwClump == clump) {
                prepare_inertia_hands_for_render(entry);
                return entry.inertia_wrist_pose_valid &&
                               entry.inertia_capture_frame == CTimer::m_FrameCounter
                           ? &entry
                           : nullptr;
            }
        }
        return nullptr;
    }

    void on_ped_render(CPed* const ped) noexcept {
        if (!settings_.enabled || !resources_ready_ || ped == nullptr || ped->m_pRwClump == nullptr) {
            return;
        }

        CPed* const player{FindPlayerPed()};
        const bool is_player{ped == player};
        if ((is_player && !settings_.enable_player) || (!is_player && !settings_.enable_npcs)) {
            return;
        }
        if (!is_player && player != nullptr) {
            const float max_distance_squared{settings_.max_distance * settings_.max_distance};
            if (squared_distance(*ped, *player) > max_distance_squared) {
                return;
            }
        }

        PedHands* const entry{find_or_create_entry(ped, is_player)};
        if (entry == nullptr) {
            return;
        }

        entry->last_seen = CTimer::m_snTimeInMilliseconds;
        const bool native_signal{has_native_hand_signal(*ped)};
        const bool render_with_ragdoll{
            inertia3d_detected_ && is_inertia_driven(*ped)};
        set_entry_visible(
            *entry,
            !native_signal && !render_with_ragdoll);
    }

    [[nodiscard]] PedHands* find_entry(const CPed* const ped) noexcept {
        for (auto& entry : entries_) {
            if (entry.ped == ped) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static bool is_inertia_driven(const CPed& ped) noexcept {
        // Inertia3D uses this flag combination throughout the physical and
        // recovery phases while its render hook owns the HAnim palette.
        return !ped.bCollidable && !ped.bApplyGravity &&
               ped.bDisableCollisionForce;
    }

    static void hide_original_hand_subtree(
        RpHAnimHierarchy* const hierarchy,
        const int forearm_index) noexcept {
        if (hierarchy == nullptr || hierarchy->pMatrixArray == nullptr ||
            hierarchy->pNodeInfo == nullptr || forearm_index < 0 ||
            forearm_index + 1 >= hierarchy->numNodes) {
            return;
        }

        // Match CHandObject::PreRender: all skinned vertices below the forearm
        // receive zero-length basis vectors, removing the rigid wrist/hand from
        // the ped while leaving the forearm and the rest of the ragdoll intact.
        int node_index{forearm_index + 1};
        int parent_stack{0};
        while (parent_stack >= 0 && node_index < hierarchy->numNodes) {
            RwMatrix& matrix{hierarchy->pMatrixArray[node_index]};
            matrix.right = {0.0F, 0.0F, 0.0F};
            matrix.up = {0.0F, 0.0F, 0.0F};
            matrix.at = {0.0F, 0.0F, 0.0F};

            const auto flags{static_cast<std::uint32_t>(
                hierarchy->pNodeInfo[node_index].flags)};
            if ((flags & rpHANIMPUSHPARENTMATRIX) != 0U) {
                ++parent_stack;
            } else if ((flags & rpHANIMPOPPARENTMATRIX) != 0U) {
                --parent_stack;
            }
            ++node_index;
        }
    }

    void prepare_inertia_hands_for_render(PedHands& entry) noexcept {
        CPed* const ped{entry.ped};
        if (!settings_.enabled || !inertia3d_detected_ || ped == nullptr ||
            ped->m_pRwClump == nullptr) {
            return;
        }

        entry.inertia_wrist_pose_valid = false;
        if (!is_inertia_driven(*ped)) {
            return;
        }

        RpHAnimHierarchy* const hierarchy{
            GetAnimHierarchyFromSkinClump(ped->m_pRwClump)};
        if (hierarchy == nullptr || hierarchy->pMatrixArray == nullptr) {
            return;
        }

        const bool native_signal{has_native_hand_signal(*ped)};
        // A CHandObject remains a separate world entity. Suppress that pass
        // while Inertia3D owns the skeleton; otherwise it can be rendered
        // before the ped and use the previous frame's wrist pose.
        set_entry_visible(entry, false);

        const int left_index{RpHAnimIDGetIndex(hierarchy, left_wrist_bone_id)};
        const int right_index{RpHAnimIDGetIndex(hierarchy, right_wrist_bone_id)};
        if (!native_signal && left_index >= 0 && right_index >= 0 &&
            left_index < hierarchy->numNodes &&
            right_index < hierarchy->numNodes) {
            RwMatrixCopy(
                &entry.inertia_left_wrist,
                &hierarchy->pMatrixArray[left_index]);
            RwMatrixCopy(
                &entry.inertia_right_wrist,
                &hierarchy->pMatrixArray[right_index]);
            entry.inertia_capture_frame = CTimer::m_FrameCounter;
            entry.inertia_wrist_pose_valid = true;
        }

        // Keep the exact wrist transforms copied above, then make the original
        // hand branches non-renderable in the palette Inertia3D just produced.
        // The flag prevents a later generic UpdateRpHAnim call from rebuilding
        // those branches before the clump is drawn; UpdateAnim resets it next
        // frame, as in the native CHandObject path.
        ped->bDontUpdateHierarchy = true;
        hide_original_hand_subtree(hierarchy, entry.left->m_nBoneIndex);
        hide_original_hand_subtree(hierarchy, entry.right->m_nBoneIndex);
    }

    void render_inertia_hands(PedHands& entry) noexcept {
        if (!entry.inertia_wrist_pose_valid ||
            entry.inertia_capture_frame != CTimer::m_FrameCounter ||
            entry.ped == nullptr || has_native_hand_signal(*entry.ped)) {
            return;
        }

        render_inertia_hand(entry.left, entry.inertia_left_wrist);
        render_inertia_hand(entry.right, entry.inertia_right_wrist);
    }

    static void render_inertia_hand(
        CHandObject* const hand,
        const RwMatrix& target_wrist) noexcept {
        if (hand == nullptr || hand->m_pRwClump == nullptr) {
            return;
        }

        const bool was_visible{hand->bIsVisible};
        const bool was_do_not_render{hand->m_nObjectFlags.bDoNotRender};
        hand->bIsVisible = true;
        hand->m_nObjectFlags.bDoNotRender = false;

        // Draw in the ped's own render pass, after Inertia3D has published the
        // physical palette for this exact frame. The hand no longer has an
        // independent render-order sample that can trail behind the ragdoll.
        align_articulated_hand(hand, target_wrist);
        plugin::CallMethod<hand_object_render_address>(hand);

        hand->bIsVisible = was_visible;
        hand->m_nObjectFlags.bDoNotRender = was_do_not_render;
    }

    void align_hand_with_inertia_pose(CObject* const object) noexcept {
        if (object == nullptr) {
            return;
        }

        for (auto& entry : entries_) {
            if (object == entry.left) {
                if (inertia3d_detected_ &&
                    entry.inertia_wrist_pose_valid &&
                    CTimer::m_FrameCounter - entry.inertia_capture_frame <= 1U) {
                    align_articulated_hand(
                        entry.left,
                        entry.inertia_left_wrist);
                }
                capture_weapon_anchor(entry, true);
                return;
            }
            if (object == entry.right) {
                if (inertia3d_detected_ &&
                    entry.inertia_wrist_pose_valid &&
                    CTimer::m_FrameCounter - entry.inertia_capture_frame <= 1U) {
                    align_articulated_hand(
                        entry.right,
                        entry.inertia_right_wrist);
                } else if (entry.fucku_wrist_pose_valid &&
                           entry.fucku_capture_frame == CTimer::m_FrameCounter) {
                    align_articulated_hand(
                        entry.right,
                        entry.fucku_right_wrist,
                        smoothstep(entry.fucku_blend));
                }
                capture_weapon_anchor(entry, false);
                return;
            }
        }
    }

    static void capture_weapon_anchor(
        PedHands& entry,
        const bool left) noexcept {
        CHandObject* const hand{left ? entry.left : entry.right};
        if (hand == nullptr || hand->m_pRwClump == nullptr) {
            return;
        }

        RpHAnimHierarchy* const hierarchy{
            GetAnimHierarchyFromSkinClump(hand->m_pRwClump)};
        if (hierarchy == nullptr || hierarchy->pMatrixArray == nullptr) {
            return;
        }
        const int hand_index{
            RpHAnimIDGetIndex(hierarchy, articulated_hand_bone_id)};
        if (hand_index < 0 || hand_index >= hierarchy->numNodes) {
            return;
        }

        const bool offset_valid{
            left ? entry.left_weapon_offset_valid
                 : entry.right_weapon_offset_valid};
        const RwMatrix& offset{
            left ? entry.left_weapon_offset : entry.right_weapon_offset};
        RwMatrix anchor{};
        if (offset_valid) {
            RwMatrixMultiply(
                &anchor,
                &hierarchy->pMatrixArray[hand_index],
                &offset);
        } else {
            RwMatrixCopy(&anchor, &hierarchy->pMatrixArray[hand_index]);
        }

        if (left) {
            RwMatrixCopy(&entry.left_weapon_anchor, &anchor);
            entry.left_weapon_anchor_frame = CTimer::m_FrameCounter;
            entry.left_weapon_anchor_valid = true;
        } else {
            RwMatrixCopy(&entry.right_weapon_anchor, &anchor);
            entry.right_weapon_anchor_frame = CTimer::m_FrameCounter;
            entry.right_weapon_anchor_valid = true;
        }
    }

    static void align_articulated_hand(
        CHandObject* const hand,
        const RwMatrix& target_wrist,
        const float blend_weight = 1.0F) noexcept {
        if (hand == nullptr || hand->m_pRwClump == nullptr) {
            return;
        }

        // CHandObject::PreRender has already evaluated the finger animation.
        // Apply one rigid world-space delta so bone 2 reaches Inertia3D's
        // physical wrist without modifying any relative finger transform.
        hand->UpdateRwFrame();
        hand->UpdateRpHAnim();
        RpHAnimHierarchy* const hierarchy{
            GetAnimHierarchyFromSkinClump(hand->m_pRwClump)};
        if (hierarchy == nullptr || hierarchy->pMatrixArray == nullptr) {
            return;
        }

        const int hand_index{
            RpHAnimIDGetIndex(hierarchy, articulated_hand_bone_id)};
        if (hand_index < 0 || hand_index >= hierarchy->numNodes) {
            return;
        }

        const RwMatrix blended_target{blend_rigid_transforms(
            hierarchy->pMatrixArray[hand_index],
            target_wrist,
            blend_weight)};
        RwMatrix inverse_hand{};
        RwMatrix correction{};
        RwMatrixInvert(&inverse_hand, &hierarchy->pMatrixArray[hand_index]);
        RwMatrixMultiply(&correction, &inverse_hand, &blended_target);
        for (int index{0}; index < hierarchy->numNodes; ++index) {
            RwMatrix corrected{};
            RwMatrixMultiply(
                &corrected,
                &hierarchy->pMatrixArray[index],
                &correction);
            RwMatrixCopy(&hierarchy->pMatrixArray[index], &corrected);
        }
    }

    [[nodiscard]] static int hidden_subtree_end(
        const RpHAnimHierarchy& hierarchy,
        const int forearm_index) noexcept {
        if (hierarchy.pNodeInfo == nullptr || forearm_index < 0 ||
            forearm_index + 1 >= hierarchy.numNodes) {
            return -1;
        }

        int node_index{forearm_index + 1};
        int parent_stack{0};
        while (parent_stack >= 0 && node_index < hierarchy.numNodes) {
            const auto flags{static_cast<std::uint32_t>(
                hierarchy.pNodeInfo[node_index].flags)};
            if ((flags & rpHANIMPUSHPARENTMATRIX) != 0U) {
                ++parent_stack;
            } else if ((flags & rpHANIMPOPPARENTMATRIX) != 0U) {
                --parent_stack;
            }
            ++node_index;
        }
        return node_index;
    }

    [[nodiscard]] static bool extract_ped_cuff_loop(
        CPed& ped,
        const int forearm_index,
        std::array<LoopPoint, max_loop_points>& loop,
        std::size_t& loop_count) noexcept {
        RpHAnimHierarchy* const hierarchy{
            GetAnimHierarchyFromSkinClump(ped.m_pRwClump)};
        if (hierarchy == nullptr) {
            return false;
        }
        const int subtree_end{hidden_subtree_end(*hierarchy, forearm_index)};
        if (subtree_end <= forearm_index + 1) {
            return false;
        }

        CuffExtractionContext context{};
        context.forearm_index = forearm_index;
        context.hidden_subtree_end = subtree_end;
        RpClumpForAllAtomics(
            ped.m_pRwClump,
            &collect_cuff_vertices,
            &context);

        loop_count = make_cuff_hull(
            context.boundary_points,
            context.boundary_count,
            loop);
        if (loop_count < 3 && context.fallback_count >= 3) {
            float distal_x{-std::numeric_limits<float>::max()};
            for (std::size_t index{0}; index < context.fallback_count; ++index) {
                distal_x = std::max(distal_x, context.fallback_points[index].x);
            }

            std::array<RwV3d, max_cuff_candidates> distal_points{};
            std::size_t distal_count{0};
            for (std::size_t index{0}; index < context.fallback_count; ++index) {
                if (context.fallback_points[index].x >= distal_x - fallback_cuff_depth) {
                    distal_points[distal_count++] = context.fallback_points[index];
                }
            }
            loop_count = make_cuff_hull(distal_points, distal_count, loop);
        }
        if (loop_count < 3) {
            return false;
        }
        sort_loop_by_angle(loop, loop_count);
        return true;
    }

    [[nodiscard]] static bool extract_hand_seam_loop(
        CHandObject& hand,
        std::array<LoopPoint, max_loop_points>& loop,
        std::size_t& loop_count) noexcept {
        RpAtomic* const source_atomic{GetFirstAtomic(hand.m_pRwClump)};
        RpGeometry* const geometry{
            source_atomic != nullptr ? RpAtomicGetGeometry(source_atomic) : nullptr};
        RpSkin* const source_skin{
            geometry != nullptr ? RpSkinGeometryGetSkin(geometry) : nullptr};
        RpMorphTarget* const morph_target{
            geometry != nullptr ? RpGeometryGetMorphTarget(geometry, 0) : nullptr};
        const RwV3d* const vertices{
            morph_target != nullptr ? RpMorphTargetGetVertices(morph_target) : nullptr};
        const RwTexCoords* const texcoords{
            geometry != nullptr
                ? RpGeometryGetVertexTexCoords(
                      geometry,
                      rwTEXTURECOORDINATEINDEX0)
                : nullptr};
        const RwUInt32* const bone_indices{
            source_skin != nullptr ? RpSkinGetVertexBoneIndices(source_skin) : nullptr};
        const RwMatrixWeights* const weights{
            source_skin != nullptr ? RpSkinGetVertexBoneWeights(source_skin) : nullptr};
        if (geometry == nullptr || source_skin == nullptr || vertices == nullptr ||
            texcoords == nullptr || bone_indices == nullptr || weights == nullptr) {
            return false;
        }

        loop_count = 0;
        const int vertex_count{RpGeometryGetNumVertices(geometry)};
        for (int vertex_index{0}; vertex_index < vertex_count; ++vertex_index) {
            if (dominant_bone(bone_indices[vertex_index], weights[vertex_index]) != 0) {
                continue;
            }

            bool duplicate{false};
            for (std::size_t existing{0}; existing < loop_count; ++existing) {
                const RwV3d delta{
                    loop[existing].position.x - vertices[vertex_index].x,
                    loop[existing].position.y - vertices[vertex_index].y,
                    loop[existing].position.z - vertices[vertex_index].z,
                };
                if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <=
                    point_merge_distance_squared) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && loop_count < loop.size()) {
                loop[loop_count].position = vertices[vertex_index];
                loop[loop_count].uv = texcoords[vertex_index];
                loop[loop_count].source_index = vertex_index;
                ++loop_count;
            }
        }
        if (loop_count < 3) {
            return false;
        }
        sort_loop_by_angle(loop, loop_count);
        return true;
    }

    static void restore_ped_geometry(
        PedGeometryOverrideSet& overrides,
        const bool reattach_original) noexcept {
        for (std::size_t index{overrides.count}; index-- > 0;) {
            PedGeometryOverride& item{overrides.items[index]};
            if (reattach_original && item.atomic != nullptr &&
                item.original_geometry != nullptr) {
                RpAtomicSetGeometry(item.atomic, item.original_geometry, 0);
            }
            if (item.original_geometry != nullptr) {
                RpGeometryDestroy(item.original_geometry);
            }
            item = {};
        }
        overrides = {};
    }

    static RpAtomic* clone_and_snap_ped_geometry(
        RpAtomic* const source_atomic,
        void* const data) noexcept {
        auto& context{*static_cast<PedSnapContext*>(data)};
        if (context.failed || context.overrides == nullptr) {
            return source_atomic;
        }

        RpGeometry* const source_geometry{
            source_atomic != nullptr ? RpAtomicGetGeometry(source_atomic) : nullptr};
        RpSkin* const source_skin{
            source_geometry != nullptr
                ? RpSkinGeometryGetSkin(source_geometry)
                : nullptr};
        RpMorphTarget* const source_morph{
            source_geometry != nullptr
                ? RpGeometryGetMorphTarget(source_geometry, 0)
                : nullptr};
        if (source_geometry == nullptr || source_morph == nullptr ||
            source_skin == nullptr) {
            return source_atomic;
        }

        const int source_vertex_count{RpGeometryGetNumVertices(source_geometry)};
        const int source_triangle_count{RpGeometryGetNumTriangles(source_geometry)};
        if (source_vertex_count <= 0 || source_triangle_count <= 0 ||
            source_vertex_count > static_cast<int>(max_ped_geometry_vertices) ||
            source_triangle_count > static_cast<int>(max_ped_geometry_triangles)) {
            context.failed = true;
            return source_atomic;
        }

        const RwV3d* const source_vertices{RpMorphTargetGetVertices(source_morph)};
        const RwV3d* const source_normals{
            RpMorphTargetGetVertexNormals(source_morph)};
        const RwRGBA* const source_colors{
            RpGeometryGetPreLightColors(source_geometry)};
        const RwUInt32* const source_bone_indices{
            RpSkinGetVertexBoneIndices(source_skin)};
        const RwMatrixWeights* const source_weights{
            RpSkinGetVertexBoneWeights(source_skin)};
        const RwMatrix* const inverse_matrices{
            RpSkinGetSkinToBoneMatrices(source_skin)};
        const RpTriangle* const source_triangles{
            RpGeometryGetTriangles(source_geometry)};
        if (source_vertices == nullptr || source_bone_indices == nullptr ||
            source_weights == nullptr || inverse_matrices == nullptr ||
            source_triangles == nullptr) {
            context.failed = true;
            return source_atomic;
        }

        std::array<RwV3d, max_ped_geometry_vertices> snapped_vertices{};
        std::array<bool, max_ped_geometry_vertices> snapped{};
        std::array<bool, 2> modified_forearms{};
        std::array<RwMatrix, 2> bone_to_skin{};
        std::array<bool, 2> valid_forearms{};
        for (std::size_t side{0}; side < context.forearms.size(); ++side) {
            const ForearmSnapTarget& target{context.forearms[side]};
            valid_forearms[side] = target.forearm_index >= 0 &&
                static_cast<RwUInt32>(target.forearm_index) <
                    RpSkinGetNumBones(source_skin) &&
                target.cuff_loop != nullptr && target.cuff_count >= 3 &&
                target.hand_loop != nullptr && target.hand_count >= 3;
            if (valid_forearms[side]) {
                RwMatrixInvert(
                    &bone_to_skin[side],
                    &inverse_matrices[target.forearm_index]);
            }
        }

        std::size_t snapped_count{0};
        for (int vertex_index{0}; vertex_index < source_vertex_count;
             ++vertex_index) {
            for (std::size_t side{0}; side < context.forearms.size(); ++side) {
                if (!valid_forearms[side]) {
                    continue;
                }
                const ForearmSnapTarget& target{context.forearms[side]};
                if (weight_for_bone(
                        source_bone_indices[vertex_index],
                        source_weights[vertex_index],
                        target.forearm_index) < minimum_forearm_weight) {
                    continue;
                }

                RwV3d forearm_local{};
                RwV3dTransformPoints(
                    &forearm_local,
                    &source_vertices[vertex_index],
                    1,
                    &inverse_matrices[target.forearm_index]);
                std::size_t cuff_index{target.cuff_count};
                float closest_distance{point_merge_distance_squared};
                for (std::size_t candidate{0}; candidate < target.cuff_count;
                     ++candidate) {
                    const RwV3d& cuff{
                        (*target.cuff_loop)[candidate].position};
                    const RwV3d delta{
                        forearm_local.x - cuff.x,
                        forearm_local.y - cuff.y,
                        forearm_local.z - cuff.z,
                    };
                    const float distance{
                        delta.x * delta.x + delta.y * delta.y +
                        delta.z * delta.z};
                    if (distance <= closest_distance) {
                        closest_distance = distance;
                        cuff_index = candidate;
                    }
                }
                if (cuff_index == target.cuff_count) {
                    continue;
                }

                const RwV3d hand_local{sample_loop(
                    *target.hand_loop,
                    target.hand_count,
                    (*target.cuff_loop)[cuff_index].angle)};
                RwV3dTransformPoints(
                    &snapped_vertices[vertex_index],
                    &hand_local,
                    1,
                    &bone_to_skin[side]);
                snapped[vertex_index] = true;
                modified_forearms[side] = true;
                ++snapped_count;
                break;
            }
        }
        if (snapped_count == 0) {
            return source_atomic;
        }
        if (context.overrides->count >= context.overrides->items.size()) {
            context.failed = true;
            return source_atomic;
        }

        RpGeometry* const geometry{RpGeometryCreate(
            source_vertex_count,
            source_triangle_count,
            RpGeometryGetFlags(source_geometry))};
        if (geometry == nullptr) {
            context.failed = true;
            return source_atomic;
        }

        RpMorphTarget* const morph_target{RpGeometryGetMorphTarget(geometry, 0)};
        RwV3d* const vertices{RpMorphTargetGetVertices(morph_target)};
        RwV3d* const normals{RpMorphTargetGetVertexNormals(morph_target)};
        RwRGBA* const colors{RpGeometryGetPreLightColors(geometry)};
        const int uv_set_count{RpGeometryGetNumTexCoordSets(source_geometry)};
        for (int vertex_index{0}; vertex_index < source_vertex_count; ++vertex_index) {
            vertices[vertex_index] = snapped[vertex_index]
                ? snapped_vertices[vertex_index]
                : source_vertices[vertex_index];
            if (normals != nullptr && source_normals != nullptr) {
                normals[vertex_index] = source_normals[vertex_index];
            }
            if (colors != nullptr && source_colors != nullptr) {
                colors[vertex_index] = source_colors[vertex_index];
            }
            for (int uv_set{1}; uv_set <= uv_set_count; ++uv_set) {
                RwTexCoords* const target_uv{
                    RpGeometryGetVertexTexCoords(geometry, uv_set)};
                const RwTexCoords* const source_uv{
                    RpGeometryGetVertexTexCoords(source_geometry, uv_set)};
                if (target_uv != nullptr && source_uv != nullptr) {
                    target_uv[vertex_index] = source_uv[vertex_index];
                }
            }
        }

        RpTriangle* const triangles{RpGeometryGetTriangles(geometry)};
        for (int triangle_index{0}; triangle_index < source_triangle_count;
             ++triangle_index) {
            const RpTriangle& source_triangle{source_triangles[triangle_index]};
            RpGeometryTriangleSetVertexIndices(
                geometry,
                &triangles[triangle_index],
                source_triangle.vertIndex[0],
                source_triangle.vertIndex[1],
                source_triangle.vertIndex[2]);
            RpGeometryTriangleSetMaterial(
                geometry,
                &triangles[triangle_index],
                RpGeometryTriangleGetMaterial(
                    source_geometry,
                    &source_triangle));
        }

        RwSphere bounds{};
        RpMorphTargetCalcBoundingSphere(morph_target, &bounds);
        RpMorphTargetSetBoundingSphere(morph_target, &bounds);
        RpGeometryUnlock(geometry);

        RpSkin* const welded_skin{RpSkinCreate(
            static_cast<RwUInt32>(source_vertex_count),
            RpSkinGetNumBones(source_skin),
            const_cast<RwMatrixWeights*>(source_weights),
            const_cast<RwUInt32*>(source_bone_indices),
            const_cast<RwMatrix*>(RpSkinGetSkinToBoneMatrices(source_skin)))};
        if (welded_skin == nullptr ||
            RpSkinGeometrySetSkin(geometry, welded_skin) == nullptr) {
            if (welded_skin != nullptr) {
                RpSkinDestroy(welded_skin);
            }
            RpGeometryDestroy(geometry);
            context.failed = true;
            return source_atomic;
        }

        ++source_geometry->refCount;
        RpGeometry* const held_original{source_geometry};
        if (RpAtomicSetGeometry(source_atomic, geometry, 0) == nullptr) {
            RpGeometryDestroy(held_original);
            RpGeometryDestroy(geometry);
            context.failed = true;
            return source_atomic;
        }
        context.overrides->items[context.overrides->count++] = {
            source_atomic,
            held_original,
        };
        RpGeometryDestroy(geometry);
        for (std::size_t side{0}; side < context.forearms.size(); ++side) {
            context.forearms[side].modified =
                context.forearms[side].modified || modified_forearms[side];
        }
        return source_atomic;
    }

    [[nodiscard]] static bool snap_ped_forearms_to_hands(
        CPed& ped,
        CHandObject& left_hand,
        CHandObject& right_hand,
        PedGeometryOverrideSet& overrides) noexcept {
        if (ped.m_pRwClump == nullptr) {
            return false;
        }
        RpHAnimHierarchy* const hierarchy{
            GetAnimHierarchyFromSkinClump(ped.m_pRwClump)};
        if (hierarchy == nullptr) {
            return false;
        }

        std::array<std::array<LoopPoint, max_loop_points>, 2> cuff_loops{};
        std::array<std::array<LoopPoint, max_loop_points>, 2> hand_loops{};
        std::array<std::size_t, 2> cuff_counts{};
        std::array<std::size_t, 2> hand_counts{};
        const std::array<CHandObject*, 2> hands{&left_hand, &right_hand};
        for (std::size_t side{0}; side < hands.size(); ++side) {
            if (!extract_ped_cuff_loop(
                    ped,
                    hands[side]->m_nBoneIndex,
                    cuff_loops[side],
                    cuff_counts[side]) ||
                !extract_hand_seam_loop(
                    *hands[side],
                    hand_loops[side],
                    hand_counts[side])) {
                return false;
            }
        }

        overrides = {};
        overrides.clump = ped.m_pRwClump;
        PedSnapContext context{};
        context.overrides = &overrides;
        for (std::size_t side{0}; side < hands.size(); ++side) {
            context.forearms[side] = {
                static_cast<int>(hands[side]->m_nBoneIndex),
                &cuff_loops[side],
                cuff_counts[side],
                &hand_loops[side],
                hand_counts[side],
                false,
            };
        }
        RpClumpForAllAtomics(
            ped.m_pRwClump,
            &clone_and_snap_ped_geometry,
            &context);
        const bool complete{
            !context.failed && context.forearms[0].modified &&
            context.forearms[1].modified};
        if (!complete) {
            restore_ped_geometry(overrides, true);
        }
        return complete;
    }

    [[nodiscard]] PedHands* find_or_create_entry(
        CPed* const ped,
        const bool is_player) noexcept {
        for (auto& entry : entries_) {
            if (entry.ped == ped) {
                return &entry;
            }
        }

        if (active_peds_ >= static_cast<std::size_t>(settings_.max_peds)) {
            if (!is_player) {
                return nullptr;
            }
            evict_oldest_entry();
        }

        for (auto& entry : entries_) {
            if (entry.ped == nullptr && create_entry(entry, ped)) {
                ++active_peds_;
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool create_entry(PedHands& entry, CPed* const ped) noexcept {
        // 0xBB4A70 is the game's global object count. Preserve room for both
        // hands and obey the same 150-object ceiling used by the original code.
        if (native_object_count() > native_object_limit - 2U) {
            return false;
        }

        const bool fat{is_fat_hand_model(*ped)};
        const int left_model{fat ? MODEL_FHANDL : MODEL_SHANDL};
        const int right_model{fat ? MODEL_FHANDR : MODEL_SHANDR};
        CHandObject* const left{create_hand(left_model, ped, true)};
        if (left == nullptr) {
            return false;
        }
        CHandObject* const right{create_hand(right_model, ped, false)};
        if (right == nullptr) {
            destroy_hand(left);
            return false;
        }

        RwTexture* const hand_texture{select_hand_texture(*ped)};
        if (hand_texture == nullptr) {
            destroy_hand(left);
            destroy_hand(right);
            return false;
        }
        left->m_pTexture = hand_texture;
        right->m_pTexture = hand_texture;
        PedGeometryOverrideSet geometry_overrides{};
        if (!snap_ped_forearms_to_hands(
                *ped,
                *left,
                *right,
                geometry_overrides)) {
            destroy_hand(left);
            destroy_hand(right);
            return false;
        }

        CAnimBlendAssociation* const left_animation{attach_finger_animation(
            left, left_grip_animation_name)};
        CAnimBlendAssociation* const right_animation{attach_finger_animation(
            right, right_grip_animation_name)};

        entry = {};
        entry.ped = ped;
        entry.left = left;
        entry.right = right;
        entry.geometry_overrides = geometry_overrides;
        entry.left_animation = left_animation;
        entry.right_animation = right_animation;
        entry.last_seen = CTimer::m_snTimeInMilliseconds;
        capture_weapon_offsets(entry);
        return true;
    }

    static void capture_weapon_offsets(PedHands& entry) noexcept {
        if (entry.ped == nullptr || entry.ped->m_pRwClump == nullptr) {
            return;
        }

        RpHAnimHierarchy* const hierarchy{
            GetAnimHierarchyFromSkinClump(entry.ped->m_pRwClump)};
        if (hierarchy == nullptr || hierarchy->pMatrixArray == nullptr) {
            return;
        }

        const auto capture_side{
            [hierarchy](
                const int wrist_bone,
                const int hand_bone,
                RwMatrix& offset) noexcept {
                const int wrist_index{
                    RpHAnimIDGetIndex(hierarchy, wrist_bone)};
                const int hand_index{RpHAnimIDGetIndex(hierarchy, hand_bone)};
                if (wrist_index < 0 || hand_index < 0 ||
                    wrist_index >= hierarchy->numNodes ||
                    hand_index >= hierarchy->numNodes) {
                    return false;
                }

                const RwMatrix& wrist_matrix{
                    hierarchy->pMatrixArray[wrist_index]};
                const RwMatrix& hand_matrix{
                    hierarchy->pMatrixArray[hand_index]};
                if (!has_valid_basis(wrist_matrix) ||
                    !has_valid_basis(hand_matrix)) {
                    return false;
                }

                RwMatrix inverse_wrist{};
                RwMatrixInvert(&inverse_wrist, &wrist_matrix);
                RwMatrixMultiply(
                    &offset,
                    &inverse_wrist,
                    &hand_matrix);
                return true;
            }};

        entry.left_weapon_offset_valid = capture_side(
            left_wrist_bone_id,
            left_hand_bone_id,
            entry.left_weapon_offset);
        entry.right_weapon_offset_valid = capture_side(
            right_wrist_bone_id,
            right_hand_bone_id,
            entry.right_weapon_offset);
    }

    [[nodiscard]] CHandObject* create_hand(
        const int model,
        CPed* const ped,
        const bool left) noexcept {
        void* const storage{CObject::operator new(sizeof(CHandObject))};
        if (storage == nullptr) {
            return nullptr;
        }

        auto* const hand{static_cast<CHandObject*>(storage)};
        CHandObject* const initialized{
            plugin::CallMethodAndReturn<CHandObject*, hand_object_constructor_address>(
                hand, model, ped, left)};
        if (initialized == nullptr) {
            CObject::operator delete(storage);
            return nullptr;
        }

        CWorld::Add(initialized);
        ++native_object_count();
        return initialized;
    }

    [[nodiscard]] CAnimBlendAssociation* attach_finger_animation(
        CHandObject* const hand,
        const char* const animation_name) const noexcept {
        CAnimBlock* const block{
            pose_anim_block_index_ >= 0
                ? &CAnimManager::ms_aAnimBlocks[pose_anim_block_index_]
                : nullptr};
        CAnimBlendHierarchy* const hierarchy{
            block != nullptr ? CAnimManager::GetAnimation(animation_name, block) : nullptr};
        if (hierarchy == nullptr) {
            return nullptr;
        }
        // This is the exact association path used by
        // CTaskSimplePlayHandSignalAnim::StartAnim for gang-sign hands.
        CAnimBlendAssociation* const association{
            CAnimManager::AddAnimation(hand->m_pRwClump, hierarchy, 0)};
        if (association == nullptr) {
            return nullptr;
        }

        association->m_bPlaying = true;
        association->m_bLooped = false;
        association->m_bFreezeLastFrame = false;
        association->m_fSpeed = 0.0F;
        association->SetCurrentTime(rest_pose_time);
        return association;
    }

    [[nodiscard]] static bool wants_closed_fist(CPed& ped) noexcept {
        if (ped.m_pIntelligence != nullptr) {
            auto& tasks{ped.m_pIntelligence->m_TaskMgr};
            if (tasks.FindActiveTaskByType(TASK_SIMPLE_FIGHT) != nullptr ||
                tasks.FindActiveTaskByType(TASK_SIMPLE_FIGHT_CTRL) != nullptr) {
                return true;
            }
        }

        const bool is_in_combat{
            ped.m_ePedState == PEDSTATE_ATTACK ||
            ped.m_ePedState == PEDSTATE_FIGHT ||
            ped.m_ePedState == PEDSTATE_AIMGUN ||
            ped.m_ePedState == PEDSTATE_SNIPER_MODE ||
            ped.m_ePedState == PEDSTATE_ROCKETLAUNCHER_MODE};
        if (is_in_combat) {
            return true;
        }

        const CWeapon* const weapon{ped.GetWeapon()};
        return weapon != nullptr && weapon->m_nState == WEAPONSTATE_FIRING;
    }

    [[nodiscard]] static bool is_playing_fucku(const CPed& ped) noexcept {
        if (ped.m_pRwClump == nullptr) {
            return false;
        }

        if (ped.m_pIntelligence != nullptr &&
            ped.m_pIntelligence->m_TaskMgr.FindActiveTaskByType(
                TASK_SIMPLE_SHAKE_FIST) != nullptr) {
            return true;
        }

        const CAnimBlendAssociation* association{
            RpAnimBlendClumpGetAssociation(ped.m_pRwClump, "FUCKU")};
        if (association == nullptr) {
            association = RpAnimBlendClumpGetAssociation(
                ped.m_pRwClump,
                static_cast<unsigned int>(ANIM_DEFAULT_FUCKU));
        }
        return association != nullptr &&
               association->m_fBlendAmount > minimum_visible_animation_blend;
    }

    static void capture_fucku_wrist(
        PedHands& entry,
        const bool fucku_active) noexcept {
        entry.fucku_wrist_pose_valid = false;
        if (!fucku_active || entry.ped == nullptr ||
            entry.ped->m_pRwClump == nullptr) {
            return;
        }

        RpHAnimHierarchy* const hierarchy{
            GetAnimHierarchyFromSkinClump(entry.ped->m_pRwClump)};
        if (hierarchy == nullptr || hierarchy->pMatrixArray == nullptr) {
            return;
        }

        const int wrist_index{
            RpHAnimIDGetIndex(hierarchy, right_wrist_bone_id)};
        if (wrist_index < 0 || wrist_index >= hierarchy->numNodes) {
            return;
        }

        const RwMatrix& wrist{hierarchy->pMatrixArray[wrist_index]};
        if (!has_valid_basis(wrist)) {
            return;
        }

        RwMatrixCopy(&entry.fucku_right_wrist, &wrist);
        entry.fucku_capture_frame = CTimer::m_FrameCounter;
        entry.fucku_wrist_pose_valid = true;
    }

    void update_finger_pose(
        PedHands& entry,
        const bool fucku_active) const noexcept {
        const float target_fucku_blend{fucku_active ? 1.0F : 0.0F};
        const float maximum_fucku_change{std::clamp(
            settings_.fucku_transition_speed * CTimer::ms_fTimeStep,
            0.001F,
            1.0F)};
        entry.fucku_blend += std::clamp(
            target_fucku_blend - entry.fucku_blend,
            -maximum_fucku_change,
            maximum_fucku_change);

        const float target_grip{wants_closed_fist(*entry.ped) ? 1.0F : 0.0F};
        const float max_change{std::clamp(
            settings_.grip_transition_speed * CTimer::ms_fTimeStep,
            0.001F,
            1.0F)};
        entry.grip += std::clamp(target_grip - entry.grip, -max_change, max_change);
        const float pose_time{
            rest_pose_time + (fist_pose_time - rest_pose_time) * entry.grip};

        if (entry.left_animation != nullptr) {
            entry.left_animation->SetCurrentTime(pose_time);
        }
        if (entry.right_animation != nullptr) {
            const float fucku_weight{smoothstep(entry.fucku_blend)};
            entry.right_animation->SetCurrentTime(
                pose_time + (fucku_pose_time - pose_time) * fucku_weight);
        }
    }

    static void set_entry_visible(PedHands& entry, const bool visible) noexcept {
        const auto apply{[visible](CHandObject* const hand) {
            if (hand != nullptr) {
                hand->bIsVisible = visible;
                hand->m_nObjectFlags.bDoNotRender = !visible;
            }
        }};
        apply(entry.left);
        apply(entry.right);
    }

    void evict_oldest_entry() noexcept {
        PedHands* oldest{nullptr};
        std::uint32_t oldest_time{std::numeric_limits<std::uint32_t>::max()};
        CPed* const player{FindPlayerPed()};
        for (auto& entry : entries_) {
            if (entry.ped != nullptr && entry.ped != player && entry.last_seen < oldest_time) {
                oldest = &entry;
                oldest_time = entry.last_seen;
            }
        }
        if (oldest != nullptr) {
            destroy_entry(*oldest);
        }
    }

    void remove_for_ped(const CPed* const ped) noexcept {
        if (ped == nullptr) {
            return;
        }
        for (auto& entry : entries_) {
            if (entry.ped == ped) {
                destroy_entry(entry);
                return;
            }
        }
    }

    void destroy_entry(PedHands& entry) noexcept {
        const bool ped_clump_is_current{
            entry.ped != nullptr &&
            entry.ped->m_pRwClump == entry.geometry_overrides.clump};
        restore_ped_geometry(
            entry.geometry_overrides,
            ped_clump_is_current);
        if (entry.left != nullptr) {
            destroy_hand(entry.left);
        }
        if (entry.right != nullptr) {
            destroy_hand(entry.right);
        }
        entry = {};
        if (active_peds_ > 0) {
            --active_peds_;
        }
    }

    static void destroy_hand(CHandObject* const hand) noexcept {
        CWorld::Remove(hand);
        plugin::CallVirtualMethod<0>(hand, 1);
        if (native_object_count() > 0) {
            --native_object_count();
        }
    }

    void release_all() noexcept {
        for (auto& entry : entries_) {
            if (entry.ped != nullptr) {
                destroy_entry(entry);
            }
        }
    }

    Settings settings_{};
    std::array<PedHands, absolute_max_peds> entries_{};
    std::array<char, MAX_PATH> ini_path_{};
    std::array<char, MAX_PATH> log_path_{};
    std::array<char, MAX_PATH> pose_animation_path_{};
    std::array<char, MAX_PATH> hand_texture_dictionary_path_{};
    std::size_t active_peds_{0};
    int anim_block_index_{-1};
    int pose_anim_block_index_{-1};
    int hand_txd_slot_{-1};
    RwTexture* dark_hand_texture_{nullptr};
    RwTexture* light_hand_texture_{nullptr};
    std::uint32_t last_cleanup_{0};
    bool resources_requested_{false};
    bool resources_ready_{false};
    bool inertia3d_detected_{false};
    bool inertia3d_detection_logged_{false};
    bool clump_render_hook_registered_{false};
    bool weapon_render_hooks_registered_{false};

    inline static SanHandsMod* active_instance_{nullptr};
    inline static ClumpRenderFunction original_clump_render_{nullptr};
    inline static ClumpRenderFunction original_right_weapon_render_{nullptr};
    inline static ClumpRenderFunction original_left_weapon_render_{nullptr};
};

SanHandsMod san_hands_mod{};

} // namespace san_hands
