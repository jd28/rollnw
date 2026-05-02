#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mudl {

enum class VfxSequenceStepKind {
    model,
    attached_model,
    beam,
    projectile,
};

enum class VfxProjectilePathKind {
    default_path,
    homing,
    ballistic,
    high_ballistic,
    burst_up,
    accelerating,
    spiral,
    linked,
    bounce,
    burst,
    linked_burst_up,
    triple_ballistic_hit,
    triple_ballistic_miss,
    double_ballistic,
};

enum class VfxProjectileOrientationKind {
    none,
    target,
    path,
};

enum class VfxProjectileTransportKind {
    none,
    moving_root,
    source_rooted_target_point,
};

enum class VfxTargetPointKind {
    none,
    point,
    root,
    center,
    anchor,
};

struct VfxSequenceStep {
    std::string label;
    std::string source;
    std::string debug_origin;
    VfxSequenceStepKind kind = VfxSequenceStepKind::model;
    int duration_ms = 1000;
    int start_offset_ms = -1;
    std::string anchor;
    std::string source_anchor;
    bool anchor_position_only = false;
    bool target_side = false;
    VfxTargetPointKind target_point_kind = VfxTargetPointKind::none;
    std::string target_anchor;
    std::string caster_animation;
    std::string animation;
    float scale = 1.0f;
    glm::vec3 start_offset{0.0f};
    glm::vec3 end_offset{0.0f};
    bool uses_target_point = false;
    VfxProjectilePathKind projectile_path = VfxProjectilePathKind::default_path;
    VfxProjectileOrientationKind projectile_orientation = VfxProjectileOrientationKind::none;
};

struct VfxSequence {
    std::string label;
    std::vector<VfxSequenceStep> steps;
    bool source_target_layout = false;
    bool use_spell_actors = false;
    float source_target_distance = 0.0f;
};

int run_nwn_animation_smoke_command(std::string_view module_path);
int run_dump_command(std::string_view resref, const std::filesystem::path& output_dir,
    std::string_view module_path);
int run_stats_command(std::string_view resref, std::string_view module_path);
int run_texture_command(std::string_view resref, const std::filesystem::path& output_path,
    std::string_view module_path);
std::optional<VfxSequence> resolve_spell_sequence(std::string_view spell_id);
std::optional<std::string> resolve_vfx_source(std::string_view query, std::string_view stage = {});
std::optional<VfxSequence> resolve_vfx_sequence(std::string_view query);

} // namespace mudl
