#include "mudl_commands.hpp"

#include "app_runtime.hpp"
#include "mudl_cli.hpp"
#include "progfx.hpp"
#include "stock_spell_fx.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/log.hpp>
#include <nw/model/mdl_particle_import.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/render/animation_backend.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/nwn/nwn_animation.hpp>
#include <nw/render/viewer/preview_plt.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/tile_light.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mudl {
namespace {

using json = nlohmann::json;
using nw::render::nwn::build_nwn_clip;
using nw::render::nwn::build_nwn_skeleton;
using nw::render::viewer::has_tile_light_slots;
using nw::render::viewer::model_light_is_main_tile_slot;
using nw::render::viewer::tile_color_from_index;
using nw::render::viewer::tile_slot_color_for_model_light;

static constexpr std::string_view kSpellSequenceTargetAnchor = "torso_g";

struct TextureInfo {
    std::string name;
    std::vector<std::string> kinds;
    std::string detail;
};

struct NwnAnimationSmokeCase {
    std::string_view resref;
    std::string_view description;
};

struct StatsReport {
    size_t total_nodes = 0;
    size_t total_meshes = 0;
    size_t total_skins = 0;
    size_t total_animmesh = 0;
    size_t total_danglymesh = 0;
    size_t total_aabb = 0;
    size_t total_emitters = 0;
    size_t total_lights = 0;
    size_t total_references = 0;
    size_t total_vertices = 0;
    size_t total_indices = 0;
    size_t total_animations = 0;
    size_t max_depth = 0;
    size_t total_skin_bones = 0;
    size_t max_skin_bones = 0;
    std::map<std::string, size_t> node_type_counts;
    std::map<std::string, size_t> dangly_policy_counts;
    std::map<std::string, TextureInfo> textures;
};

struct LightNodeStats {
    const nw::model::LightNode* node = nullptr;
    std::optional<glm::vec3> controller_color;
    std::optional<float> controller_radius;
    std::optional<float> controller_shadow_radius;
    std::optional<float> controller_multiplier;
};

using TileLightSlots = nw::render::viewer::SceneTileLightSlots;

struct AreaLightTuning {
    float radius_scale = 1.0f;
    float intensity_scale = 1.0f;
};

struct AreaLightSample {
    int tile_x = 0;
    int tile_y = 0;
    int tile_id = 0;
    int tile_height = 0;
    int tile_orientation = 0;
    std::string model;
    std::string node;
    TileLightSlots slots{};
    const char* slot_kind = "source";
    glm::vec3 position{0.0f};
    glm::vec3 color{0.0f};
    float authored_radius = 0.0f;
    float radius = 0.0f;
    float authored_multiplier = 0.0f;
    float intensity = 0.0f;
};

struct AreaPlaceableLightSample {
    size_t index = 0;
    uint32_t appearance = 0;
    std::string tag;
    std::string resref;
    std::string label;
    std::string model;
    int32_t light_color = -1;
    glm::vec3 offset{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 color{0.0f};
};

struct AreaLightReport {
    std::string area;
    std::string tileset;
    int width = 0;
    int height = 0;
    bool interior = false;
    bool underground = false;
    bool day_night_cycle = false;
    bool night = false;
    size_t tiles = 0;
    size_t tiles_with_light_slots = 0;
    size_t unique_slot_sets = 0;
    size_t tile_model_lights = 0;
    size_t colored_lights = 0;
    size_t placeable_table_lights = 0;
    size_t placeable_colored_lights = 0;
    size_t main_like_lights = 0;
    size_t source_like_lights = 0;
    size_t missing_models = 0;
    float max_color = 0.0f;
    float placeable_max_color = 0.0f;
    float min_radius = 0.0f;
    float max_radius = 0.0f;
    float min_intensity = 0.0f;
    float max_intensity = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    AreaLightTuning tuning{};
    std::map<std::string, size_t> model_counts;
    std::map<std::array<uint8_t, 4>, size_t> slot_counts;
    std::map<int32_t, size_t> placeable_light_color_counts;
    std::vector<AreaLightSample> samples;
    std::vector<AreaPlaceableLightSample> placeable_samples;
};

struct DumpResource {
    nw::Resource resource;
    std::set<std::string> origins;
    bool copied = false;
    bool missing = false;
    size_t bytes = 0;
};

struct DumpReport {
    std::map<std::string, DumpResource> resources;
    std::vector<nw::Resource> scan_queue;
    std::set<std::string> scanned_text_resources;
    std::set<std::string> scanned_models;
    std::set<std::string> scanned_model_visuals;
};

enum class VfxLookupStage {
    auto_select,
    projectile,
    cast,
    conjure,
    impact,
    duration,
    cessation,
};

std::optional<std::string> resolve_visualeffect_vfx_source(std::string_view query, VfxLookupStage stage);

std::string format_debug_origin(std::string_view table, size_t row, std::string_view detail)
{
    std::string result = std::string(table) + "[" + std::to_string(row) + "]";
    if (!detail.empty()) {
        result += " ";
        result += detail;
    }
    return result;
}

float max_curve_value(const nw::render::ParticleCurveF32& curve, float fallback)
{
    float result = fallback;
    for (const auto& key : curve.keys) {
        result = std::max(result, key.value);
    }
    return result;
}

int estimate_burst_step_duration_ms(std::string_view model_spec, int authored_ms)
{
    auto* mdl = nw::kernel::models().load(model_spec);
    if (!mdl) {
        return authored_ms;
    }

    const auto animation_name = nw::render::viewer::preferred_model_animation_name(
        *mdl, nw::render::viewer::PreferredModelAnimationContext::sequence_effect);
    const auto import = nw::model::import_particle_effect(*mdl, animation_name, false);

    float max_lifetime_s = 0.0f;
    bool has_event_burst = false;
    for (const auto& emitter : import.effect.emitters) {
        if (emitter.emission.mode != nw::render::ParticleEmissionMode::event_burst) {
            continue;
        }

        has_event_burst = true;
        max_lifetime_s = std::max(max_lifetime_s, emitter.initial.lifetime.max);
        max_lifetime_s = std::max(max_lifetime_s, max_curve_value(emitter.spawn_over_time.lifetime, emitter.initial.lifetime.max));
    }

    if (!has_event_burst) {
        return authored_ms;
    }

    float last_event_time_s = 0.0f;
    for (const auto& event : import.effect_events) {
        last_event_time_s = std::max(last_event_time_s, event.time);
    }

    const int settle_ms = static_cast<int>(std::ceil((last_event_time_s + max_lifetime_s) * 1000.0f));
    return std::max(authored_ms, settle_ms);
}

std::optional<int> preferred_model_animation_duration_ms(std::string_view model_spec)
{
    auto* mdl = nw::kernel::models().load(model_spec);
    if (!mdl) {
        return std::nullopt;
    }

    const auto animation_name = nw::render::viewer::preferred_model_animation_name(
        *mdl, nw::render::viewer::PreferredModelAnimationContext::sequence_effect);
    if (animation_name.empty()) {
        return std::nullopt;
    }

    const auto* animation = mdl->model.find_animation(animation_name);
    if (!animation || animation->length <= 0.0f) {
        return std::nullopt;
    }

    return std::max(1, static_cast<int>(std::lround(animation->length * 1000.0f)));
}

const nw::model::Animation* find_nwn_smoke_animation(const nw::model::Mdl& mdl)
{
    static constexpr std::array<std::string_view, 4> preferred_animations{{
        "shieldl",
        "pausesh",
        "pause1",
        "cpause1",
    }};

    const nw::model::Mdl* current = &mdl;
    while (current) {
        for (const auto animation_name : preferred_animations) {
            for (const auto& animation : current->model.animations) {
                if (animation && nw::string::icmp(animation->name, animation_name)) {
                    return animation.get();
                }
            }
        }
        if (!current->model.supermodel) {
            break;
        }
        current = current->model.supermodel.get();
    }
    return nullptr;
}

std::string normalize_vfx_key(std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

std::optional<VfxLookupStage> parse_vfx_stage(std::string_view value)
{
    const auto stage = normalize_vfx_key(value);
    if (stage == "proj" || stage == "projectile") {
        return VfxLookupStage::projectile;
    }
    if (stage == "cast") {
        return VfxLookupStage::cast;
    }
    if (stage == "conj" || stage == "conjure" || stage == "conjuration") {
        return VfxLookupStage::conjure;
    }
    if (stage == "imp" || stage == "impact") {
        return VfxLookupStage::impact;
    }
    if (stage == "dur" || stage == "duration") {
        return VfxLookupStage::duration;
    }
    if (stage == "ces" || stage == "cessation" || stage == "end") {
        return VfxLookupStage::cessation;
    }
    return std::nullopt;
}

bool resource_exists(std::string_view resref)
{
    return !resref.empty() && nw::kernel::resman().contains({nw::Resref{resref}, nw::ResourceType::mdl});
}

std::optional<VfxLookupStage> resolve_vfx_stage(std::string_view stage)
{
    if (stage.empty()) {
        return VfxLookupStage::auto_select;
    }
    return parse_vfx_stage(stage);
}

std::optional<size_t> find_2da_row_by_label(const nw::StaticTwoDA& table, std::string_view query)
{
    const auto normalized_query = normalize_vfx_key(query);
    if (normalized_query.empty()) {
        return std::nullopt;
    }

    nw::String label;
    for (size_t i = 0; i < table.rows(); ++i) {
        if (!table.get_to(i, "Label", label, false)) {
            continue;
        }
        if (normalize_vfx_key(label) == normalized_query) {
            return i;
        }
    }

    return std::nullopt;
}

std::optional<size_t> parse_2da_row_index(std::string_view value, const nw::StaticTwoDA& table)
{
    if (value.empty()) {
        return std::nullopt;
    }

    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
    }

    auto parsed = nw::string::from<size_t>(value);
    if (!parsed || *parsed >= table.rows()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string> resolve_model_from_2da_row(const nw::StaticTwoDA& table, size_t row,
    std::string_view label, std::initializer_list<std::string_view> columns)
{
    for (auto column : columns) {
        nw::String resref;
        if (!table.get_to(row, column, resref, false) || resref.empty()) {
            continue;
        }
        if (!resource_exists(resref)) {
            continue;
        }
        LOG_F(INFO, "Resolved VFX '{}' via {} -> {}", label, column, resref);
        return std::string(resref);
    }

    return std::nullopt;
}

std::optional<std::string> resolve_progfx_vfx_source(const nw::StaticTwoDA& progfx, size_t row, std::string_view source_label)
{
    if (auto def = parse_progfx_def(progfx, row, source_label)) {
        return def->model;
    }
    return std::nullopt;
}

std::optional<std::string> resolve_spell_vfx_reference(const nw::StaticTwoDA& spells, size_t row,
    std::string_view spell_label, std::initializer_list<std::string_view> columns)
{
    for (auto column : columns) {
        nw::String value;
        if (!spells.get_to(row, column, value, false) || value.empty()) {
            continue;
        }
        if (resource_exists(value)) {
            LOG_F(INFO, "Resolved VFX spell '{}' via {} -> {}", spell_label, column, value);
            return std::string(value);
        }
        if (auto resolved = resolve_visualeffect_vfx_source(value, VfxLookupStage::auto_select)) {
            LOG_F(INFO, "Resolved VFX spell '{}' via {} -> {} -> {}", spell_label, column, value, *resolved);
            return resolved;
        }
    }

    return std::nullopt;
}

std::optional<std::string> resolve_spell_vfx_column(const nw::StaticTwoDA& spells, size_t row,
    std::string_view spell_label, std::string_view column)
{
    nw::String value;
    if (!spells.get_to(row, column, value, false) || value.empty()) {
        return std::nullopt;
    }

    if (resource_exists(value)) {
        LOG_F(INFO, "Resolved VFX spell '{}' via {} -> {}", spell_label, column, value);
        return std::string(value);
    }

    if (auto resolved = resolve_visualeffect_vfx_source(value, VfxLookupStage::auto_select)) {
        LOG_F(INFO, "Resolved VFX spell '{}' via {} -> {} -> {}", spell_label, column, value, *resolved);
        return resolved;
    }

    return std::nullopt;
}

VfxSequenceStep make_spell_vfx_step(std::string_view label, std::string source, std::string_view anchor)
{
    VfxSequenceStep step;
    step.label = std::string(label);
    step.source = std::move(source);
    step.duration_ms = 1000;
    step.anchor = std::string(anchor);
    step.anchor_position_only = true;
    step.scale = 1.0f;
    return step;
}

int default_vfx_stage_duration_ms(VfxLookupStage stage, std::string_view model_spec)
{
    switch (stage) {
    case VfxLookupStage::impact:
    case VfxLookupStage::cessation:
        return estimate_burst_step_duration_ms(model_spec, 1000);
    case VfxLookupStage::duration:
        if (auto duration = preferred_model_animation_duration_ms(model_spec)) {
            return std::max(*duration, 1000);
        }
        return 3000;
    case VfxLookupStage::cast:
    case VfxLookupStage::conjure:
    case VfxLookupStage::projectile:
    case VfxLookupStage::auto_select:
        break;
    }
    return 1000;
}

float base_projectile_velocity(float distance_meters, ProgFxTravelTiming timing)
{
    switch (timing) {
    case ProgFxTravelTiming::linear:
        return 50.0f;
    case ProgFxTravelTiming::linear2:
        return 25.0f;
    case ProgFxTravelTiming::log:
        break;
    }

    const float distance = std::max(distance_meters, 0.1f);
    return 3.0f * std::log(distance) + 2.0f;
}

int estimate_projectile_duration_ms(float distance_meters, std::string_view projectile_model,
    VfxProjectilePathKind path, ProgFxTravelTiming timing)
{
    int authored_ms = 0;
    if (!projectile_model.empty()) {
        if (auto value = preferred_model_animation_duration_ms(projectile_model)) {
            authored_ms = *value;
        }
    }

    const float distance = std::max(distance_meters, 0.1f);
    float velocity = base_projectile_velocity(distance, timing);
    float extra_ms = 0.0f;

    switch (path) {
    case VfxProjectilePathKind::homing:
        velocity *= 2.0f;
        break;
    case VfxProjectilePathKind::accelerating:
        velocity *= 1.5f;
        break;
    case VfxProjectilePathKind::bounce:
        velocity *= 0.8f;
        break;
    case VfxProjectilePathKind::linked:
        velocity = distance / 2.0f;
        break;
    case VfxProjectilePathKind::spiral:
        extra_ms = 2500.0f;
        break;
    case VfxProjectilePathKind::default_path:
    case VfxProjectilePathKind::ballistic:
    case VfxProjectilePathKind::high_ballistic:
    case VfxProjectilePathKind::burst_up:
    case VfxProjectilePathKind::burst:
    case VfxProjectilePathKind::linked_burst_up:
    case VfxProjectilePathKind::triple_ballistic_hit:
    case VfxProjectilePathKind::triple_ballistic_miss:
    case VfxProjectilePathKind::double_ballistic:
        break;
    }

    velocity = std::max(velocity, 0.1f);
    const int travel_ms = std::max(1, static_cast<int>(std::lround(distance / velocity * 1000.0f + extra_ms)));
    return std::max(travel_ms, authored_ms);
}

VfxProjectilePathKind spell_projectile_path(std::string_view value)
{
    if (nw::string::icmp(value, "homing")) {
        return VfxProjectilePathKind::homing;
    }
    if (nw::string::icmp(value, "accelerating")) {
        return VfxProjectilePathKind::accelerating;
    }
    if (nw::string::icmp(value, "bounce")) {
        return VfxProjectilePathKind::bounce;
    }
    if (nw::string::icmp(value, "linked")) {
        return VfxProjectilePathKind::linked;
    }
    if (nw::string::icmp(value, "spiral")) {
        return VfxProjectilePathKind::spiral;
    }
    if (nw::string::icmp(value, "ballistic")) {
        return VfxProjectilePathKind::ballistic;
    }
    if (nw::string::icmp(value, "highballistic")) {
        return VfxProjectilePathKind::high_ballistic;
    }
    if (nw::string::icmp(value, "burst")) {
        return VfxProjectilePathKind::burst;
    }
    return VfxProjectilePathKind::default_path;
}

VfxProjectileOrientationKind spell_projectile_orientation(std::string_view value)
{
    if (nw::string::icmp(value, "target")) {
        return VfxProjectileOrientationKind::target;
    }
    if (nw::string::icmp(value, "path")) {
        return VfxProjectileOrientationKind::path;
    }
    return VfxProjectileOrientationKind::none;
}

int sequence_end_ms(const VfxSequence& sequence)
{
    int cursor_ms = 0;
    int max_end_ms = 0;
    for (const auto& step : sequence.steps) {
        const int start_ms = step.start_offset_ms >= 0 ? step.start_offset_ms : cursor_ms;
        const int end_ms = start_ms + std::max(step.duration_ms, 1);
        cursor_ms = std::max(cursor_ms, end_ms);
        max_end_ms = std::max(max_end_ms, end_ms);
    }
    return max_end_ms;
}

std::string preferred_effect_source_anchor(std::string_view model_spec)
{
    auto* mdl = nw::kernel::models().load(model_spec);
    if (!mdl) {
        return {};
    }

    const auto animation_name = nw::render::viewer::preferred_model_animation_name(
        *mdl, nw::render::viewer::PreferredModelAnimationContext::sequence_effect);
    const auto import = nw::model::import_particle_effect(*mdl, animation_name, false);
    for (const auto& init : import.emitter_inits) {
        if (!init.emitter_node_name.empty()) {
            return init.emitter_node_name;
        }
    }

    return {};
}

std::optional<VfxSequenceStep> resolve_spell_vfx_step(const nw::StaticTwoDA& spells, size_t row,
    std::string_view spell_label, std::string_view label,
    std::initializer_list<std::pair<std::string_view, std::string_view>> columns)
{
    for (const auto& [column, anchor] : columns) {
        if (auto resolved = resolve_spell_vfx_column(spells, row, spell_label, column)) {
            auto step = make_spell_vfx_step(label, std::move(*resolved), anchor);
            step.debug_origin = format_debug_origin("spells", row, column);
            return step;
        }
    }

    return std::nullopt;
}

std::optional<VfxSequenceStep> resolve_conjure_spell_vfx_step(const nw::StaticTwoDA& spells, size_t row,
    std::string_view spell_label, std::string_view label, std::string_view conj_anim)
{
    auto hand = resolve_spell_vfx_column(spells, row, spell_label, "ConjHandVisual");
    auto head = resolve_spell_vfx_column(spells, row, spell_label, "ConjHeadVisual");
    auto ground = resolve_spell_vfx_column(spells, row, spell_label, "ConjGrndVisual");

    if (nw::string::icmp(conj_anim, "head")) {
        if (head) {
            auto step = make_spell_vfx_step(label, std::move(*head), "headconjure");
            step.debug_origin = format_debug_origin("spells", row, "ConjHeadVisual");
            return step;
        }
        if (hand) {
            auto step = make_spell_vfx_step(label, std::move(*hand), "headconjure");
            step.source_anchor = preferred_effect_source_anchor(step.source);
            step.debug_origin = format_debug_origin("spells", row, "ConjHandVisual -> head fallback");
            return step;
        }
        if (ground) {
            auto step = make_spell_vfx_step(label, std::move(*ground), "rootdummy");
            step.debug_origin = format_debug_origin("spells", row, "ConjGrndVisual");
            return step;
        }
        return std::nullopt;
    }

    if (nw::string::icmp(conj_anim, "hand")) {
        if (hand) {
            auto step = make_spell_vfx_step(label, std::move(*hand), "handconjure");
            step.debug_origin = format_debug_origin("spells", row, "ConjHandVisual");
            return step;
        }
        if (head) {
            auto step = make_spell_vfx_step(label, std::move(*head), "handconjure");
            step.source_anchor = preferred_effect_source_anchor(step.source);
            step.debug_origin = format_debug_origin("spells", row, "ConjHeadVisual -> hand fallback");
            return step;
        }
        if (ground) {
            auto step = make_spell_vfx_step(label, std::move(*ground), "rootdummy");
            step.debug_origin = format_debug_origin("spells", row, "ConjGrndVisual");
            return step;
        }
        return std::nullopt;
    }

    if (hand) {
        auto step = make_spell_vfx_step(label, std::move(*hand), "handconjure");
        step.debug_origin = format_debug_origin("spells", row, "ConjHandVisual");
        return step;
    }
    if (head) {
        auto step = make_spell_vfx_step(label, std::move(*head), "headconjure");
        step.debug_origin = format_debug_origin("spells", row, "ConjHeadVisual");
        return step;
    }
    if (ground) {
        auto step = make_spell_vfx_step(label, std::move(*ground), "rootdummy");
        step.debug_origin = format_debug_origin("spells", row, "ConjGrndVisual");
        return step;
    }

    return std::nullopt;
}

std::optional<int> get_spell_time_ms(const nw::StaticTwoDA& spells, size_t row, std::string_view column)
{
    int value = 0;
    if (!spells.get_to(row, column, value, false) || value <= 0) {
        return std::nullopt;
    }
    return value;
}

void append_sequence_step(VfxSequence& sequence, std::optional<VfxSequenceStep> step, int duration_ms)
{
    if (!step || step->source.empty()) {
        return;
    }

    step->duration_ms = std::max(duration_ms, 1);
    if (!sequence.steps.empty()
        && sequence.steps.back().source == step->source
        && sequence.steps.back().anchor == step->anchor) {
        sequence.steps.back().duration_ms += step->duration_ms;
        return;
    }

    sequence.steps.push_back(std::move(*step));
}

std::string map_conjure_animation(std::string_view value)
{
    if (nw::string::icmp(value, "head")) {
        return "conjure2";
    }
    return "conjure1";
}

std::string map_cast_animation(std::string_view value)
{
    if (nw::string::icmp(value, "area")) {
        return "castarea";
    }
    if (nw::string::icmp(value, "out")) {
        return "castout";
    }
    if (nw::string::icmp(value, "self")) {
        return "castself";
    }
    if (nw::string::icmp(value, "up")) {
        return "castup";
    }
    if (nw::string::icmp(value, "touch") || nw::string::icmp(value, "point")) {
        return "castpoint";
    }
    return {};
}

std::string map_projectile_anchor(std::string_view value)
{
    if (nw::string::icmp(value, "head")) {
        return "headconjure";
    }
    if (nw::string::icmp(value, "root")) {
        return "rootdummy";
    }
    if (nw::string::icmp(value, "hand") || value.empty()) {
        return "handconjure";
    }
    return "rootdummy";
}

int estimate_spell_projectile_duration_ms(const nw::StaticTwoDA& spells, size_t row, float distance_meters,
    std::string_view projectile_model)
{
    nw::String proj_type;
    spells.get_to(row, "ProjType", proj_type, false);
    return estimate_projectile_duration_ms(
        distance_meters, projectile_model, spell_projectile_path(proj_type), ProgFxTravelTiming::log);
}

std::optional<std::string> resolve_visualeffect_progfx_source(const nw::StaticTwoDA& visualeffects, size_t row,
    std::string_view label, std::initializer_list<std::string_view> columns)
{
    const auto* progfx = nw::kernel::twodas().get("progfx");
    if (!progfx) {
        return std::nullopt;
    }

    for (auto column : columns) {
        int progfx_row = 0;
        if (!visualeffects.get_to(row, column, progfx_row, false) || progfx_row < 0
            || static_cast<size_t>(progfx_row) >= progfx->rows()) {
            continue;
        }

        if (auto resolved = resolve_progfx_vfx_source(*progfx, static_cast<size_t>(progfx_row), label)) {
            LOG_F(INFO, "Resolved VFX '{}' via {} -> progfx[{}] -> {}", label, column, progfx_row, *resolved);
            return resolved;
        }
    }

    return std::nullopt;
}

std::optional<VfxSequenceStep> make_progfx_sequence_step(const ProgFxDef& def, VfxLookupStage stage,
    float source_target_distance, size_t visualeffects_row, std::string_view column)
{
    VfxSequenceStep step;
    step.label = std::string(def.label);
    step.source = def.model;
    step.debug_origin = format_debug_origin(
        "visualeffects", visualeffects_row,
        std::string(column) + " -> progfx[" + std::to_string(def.row) + "] type " + std::to_string(def.type));
    step.kind = def.step_kind;
    step.animation = def.animation;
    step.scale = 1.0f;
    step.projectile_path = def.projectile_path;
    step.projectile_orientation = def.projectile_orientation;

    switch (def.step_kind) {
    case VfxSequenceStepKind::projectile:
        step.duration_ms = estimate_projectile_duration_ms(
            source_target_distance, def.model, def.projectile_path, def.projectile_timing);
        step.anchor = "handconjure";
        step.source_anchor = preferred_effect_source_anchor(step.source);
        step.target_side = true;
        step.target_point_kind = VfxTargetPointKind::point;
        break;
    case VfxSequenceStepKind::beam:
        step.duration_ms = default_vfx_stage_duration_ms(stage, def.model);
        step.uses_target_point = true;
        step.target_point_kind = VfxTargetPointKind::point;
        break;
    case VfxSequenceStepKind::attached_model:
        step.duration_ms = default_vfx_stage_duration_ms(stage, def.model);
        step.anchor = def.node;
        step.anchor_position_only = false;
        break;
    case VfxSequenceStepKind::model:
        step.duration_ms = default_vfx_stage_duration_ms(stage, def.model);
        step.target_side = true;
        break;
    }

    return step;
}

std::optional<VfxSequenceStep> resolve_visualeffect_direct_step(const nw::StaticTwoDA& visualeffects, size_t row,
    std::string_view label, VfxLookupStage stage)
{
    switch (stage) {
    case VfxLookupStage::impact:
        if (auto resolved = resolve_model_from_2da_row(visualeffects, row, label, {
            "Imp_HeadCon_Node",
            "Imp_Impact_Node",
            "Imp_Root_M_Node",
            "Imp_Root_L_Node",
            "Imp_Root_H_Node",
            "Imp_Root_S_Node",
            "LowQuality",
            "LowViolence",
        })) {
            VfxSequenceStep step;
            step.label = std::string(label);
            step.source = std::move(*resolved);
            step.debug_origin = format_debug_origin("visualeffects", row, "impact direct");
            step.kind = VfxSequenceStepKind::model;
            step.duration_ms = default_vfx_stage_duration_ms(stage, step.source);
            step.target_side = true;
            return step;
        }
        return std::nullopt;
    case VfxLookupStage::duration:
        if (auto resolved = resolve_model_from_2da_row(visualeffects, row, label, {
            "Imp_HeadCon_Node",
            "Imp_Impact_Node",
            "Imp_Root_M_Node",
            "Imp_Root_L_Node",
            "Imp_Root_H_Node",
            "Imp_Root_S_Node",
            "LowQuality",
            "LowViolence",
        })) {
            VfxSequenceStep step;
            step.label = std::string(label);
            step.source = std::move(*resolved);
            step.debug_origin = format_debug_origin("visualeffects", row, "duration direct");
            step.kind = VfxSequenceStepKind::model;
            step.duration_ms = default_vfx_stage_duration_ms(stage, step.source);
            step.target_side = true;
            return step;
        }
        return std::nullopt;
    case VfxLookupStage::cessation:
        if (auto resolved = resolve_model_from_2da_row(visualeffects, row, label, {
            "Ces_HeadCon_Node",
            "Ces_Impact_Node",
            "Ces_Root_M_Node",
            "Ces_Root_L_Node",
            "Ces_Root_H_Node",
            "Ces_Root_S_Node",
        })) {
            VfxSequenceStep step;
            step.label = std::string(label);
            step.source = std::move(*resolved);
            step.debug_origin = format_debug_origin("visualeffects", row, "cessation direct");
            step.kind = VfxSequenceStepKind::model;
            step.duration_ms = default_vfx_stage_duration_ms(stage, step.source);
            step.target_side = true;
            return step;
        }
        return std::nullopt;
    case VfxLookupStage::projectile:
    case VfxLookupStage::cast:
    case VfxLookupStage::conjure:
    case VfxLookupStage::auto_select:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<VfxSequenceStep> resolve_visualeffect_progfx_step(const nw::StaticTwoDA& visualeffects, size_t row,
    std::string_view label, VfxLookupStage stage, float source_target_distance)
{
    const auto* progfx = nw::kernel::twodas().get("progfx");
    if (!progfx) {
        return std::nullopt;
    }

    switch (stage) {
    case VfxLookupStage::impact:
        for (auto column : {"ProgFX_Impact"}) {
            int progfx_row = 0;
            if (!visualeffects.get_to(row, column, progfx_row, false) || progfx_row < 0
                || static_cast<size_t>(progfx_row) >= progfx->rows()) {
                continue;
            }
            if (auto def = parse_progfx_def(*progfx, static_cast<size_t>(progfx_row), label)) {
                auto step = make_progfx_sequence_step(*def, stage, source_target_distance, row, column);
                if (step) {
                    step->label = std::string(column);
                    LOG_F(INFO, "Resolved VFX '{}' via {} -> progfx[{}] -> {}", label, column, progfx_row, def->model);
                }
                return step;
            }
        }
        break;
    case VfxLookupStage::duration:
        for (auto column : {"ProgFX_Duration"}) {
            int progfx_row = 0;
            if (!visualeffects.get_to(row, column, progfx_row, false) || progfx_row < 0
                || static_cast<size_t>(progfx_row) >= progfx->rows()) {
                continue;
            }
            if (auto def = parse_progfx_def(*progfx, static_cast<size_t>(progfx_row), label)) {
                auto step = make_progfx_sequence_step(*def, stage, source_target_distance, row, column);
                if (step) {
                    step->label = std::string(column);
                    LOG_F(INFO, "Resolved VFX '{}' via {} -> progfx[{}] -> {}", label, column, progfx_row, def->model);
                }
                return step;
            }
        }
        break;
    case VfxLookupStage::cessation:
        for (auto column : {"ProgFX_Cessation"}) {
            int progfx_row = 0;
            if (!visualeffects.get_to(row, column, progfx_row, false) || progfx_row < 0
                || static_cast<size_t>(progfx_row) >= progfx->rows()) {
                continue;
            }
            if (auto def = parse_progfx_def(*progfx, static_cast<size_t>(progfx_row), label)) {
                auto step = make_progfx_sequence_step(*def, stage, source_target_distance, row, column);
                if (step) {
                    step->label = std::string(column);
                    LOG_F(INFO, "Resolved VFX '{}' via {} -> progfx[{}] -> {}", label, column, progfx_row, def->model);
                }
                return step;
            }
        }
        break;
    case VfxLookupStage::projectile:
    case VfxLookupStage::cast:
    case VfxLookupStage::conjure:
    case VfxLookupStage::auto_select:
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<VfxSequence> build_visualeffect_sequence(const nw::StaticTwoDA& visualeffects, size_t row)
{
    nw::String label;
    visualeffects.get_to(row, "Label", label, false);

    VfxSequence sequence;
    sequence.label = std::string(label);
    sequence.source_target_distance = 15.0f;

    for (const auto stage : {VfxLookupStage::impact, VfxLookupStage::duration, VfxLookupStage::cessation}) {
        if (auto step = resolve_visualeffect_direct_step(visualeffects, row, label, stage)) {
            const int duration_ms = step->duration_ms;
            append_sequence_step(sequence, std::move(step), duration_ms);
            continue;
        }
        if (auto step = resolve_visualeffect_progfx_step(
                visualeffects, row, label, stage, sequence.source_target_distance)) {
            const int duration_ms = step->duration_ms;
            append_sequence_step(sequence, std::move(step), duration_ms);
        }
    }

    if (sequence.steps.empty()) {
        return std::nullopt;
    }

    sequence.source_target_layout = std::any_of(sequence.steps.begin(), sequence.steps.end(), [](const auto& step) {
        return step.kind == VfxSequenceStepKind::beam
            || step.kind == VfxSequenceStepKind::projectile
            || step.target_side;
    });
    return sequence;
}

std::optional<VfxSequenceStep> resolve_scripted_spell_projectile_step(const nw::StaticTwoDA& spells, size_t row,
    std::string_view spell_label, float source_target_distance, const StockSpellProjectileFx& compatibility)
{
    int has_projectile = 0;
    if (!spells.get_to(row, "HasProjectile", has_projectile, false) || has_projectile == 0) {
        return std::nullopt;
    }

    nw::String impact_script;
    if (!spells.get_to(row, "ImpactScript", impact_script, false) || impact_script.empty()) {
        return std::nullopt;
    }

    if (!nw::string::icmp(impact_script, compatibility.impact_script)) {
        return std::nullopt;
    }

    const auto* visualeffects = nw::kernel::twodas().get("visualeffects");
    if (!visualeffects) {
        return std::nullopt;
    }

    const auto vfx_row = find_2da_row_by_label(*visualeffects, compatibility.vfx_label);
    if (!vfx_row) {
        return std::nullopt;
    }

    auto step = resolve_visualeffect_progfx_step(
        *visualeffects, *vfx_row, compatibility.vfx_label, VfxLookupStage::impact, source_target_distance);
    if (!step) {
        return std::nullopt;
    }

    step->label = "projectile";
    step->target_side = true;
    step->debug_origin = format_debug_origin(
        "spells", row, std::string("ImpactScript ") + std::string(impact_script)
            + " -> stock_spell_fx -> " + std::string(compatibility.vfx_label));
    LOG_F(INFO, "Resolved VFX spell '{}' scripted projectile via {} -> {}",
        spell_label, impact_script, compatibility.vfx_label);
    return step;
}

std::optional<std::string> resolve_spell_vfx_source(std::string_view query, VfxLookupStage stage)
{
    const auto* spells = nw::kernel::twodas().get("spells");
    if (!spells) {
        return std::nullopt;
    }

    auto row = find_2da_row_by_label(*spells, query);
    if (!row) {
        return std::nullopt;
    }

    nw::String label;
    spells->get_to(*row, "Label", label, false);

    switch (stage) {
    case VfxLookupStage::projectile:
        return resolve_model_from_2da_row(*spells, *row, label, {"ProjModel"});
    case VfxLookupStage::cast:
        if (auto resolved = resolve_spell_vfx_reference(*spells, *row, label, {"CastHandVisual", "CastHeadVisual", "CastGrndVisual"})) {
            return resolved;
        }
        return resolve_spell_vfx_reference(*spells, *row, label, {"ConjHandVisual", "ConjHeadVisual", "ConjGrndVisual"});
    case VfxLookupStage::conjure:
        return resolve_spell_vfx_reference(*spells, *row, label, {"ConjHandVisual", "ConjHeadVisual", "ConjGrndVisual"});
    case VfxLookupStage::impact:
    case VfxLookupStage::duration:
    case VfxLookupStage::cessation:
        return std::nullopt;
    case VfxLookupStage::auto_select:
        if (auto resolved = resolve_model_from_2da_row(*spells, *row, label, {"ProjModel"})) {
            return resolved;
        }
        if (auto resolved = resolve_spell_vfx_reference(*spells, *row, label, {"CastHandVisual", "CastHeadVisual", "CastGrndVisual"})) {
            return resolved;
        }
        return resolve_spell_vfx_reference(*spells, *row, label, {"ConjHandVisual", "ConjHeadVisual", "ConjGrndVisual"});
    }

    return std::nullopt;
}

std::optional<VfxSequence> build_spell_sequence(const nw::StaticTwoDA& spells, size_t row)
{
    nw::String label;
    spells.get_to(row, "Label", label, false);

    VfxSequence sequence;
    sequence.label = std::string(label);
    sequence.source_target_layout = true;
    sequence.use_spell_actors = true;
    sequence.source_target_distance = 10.0f;
    nw::String conj_anim;
    spells.get_to(row, "ConjAnim", conj_anim, false);
    nw::String cast_anim;
    spells.get_to(row, "CastAnim", cast_anim, false);
    nw::String proj_spawn_point;
    spells.get_to(row, "ProjSpwnPoint", proj_spawn_point, false);
    nw::String proj_orientation;
    spells.get_to(row, "ProjOrientation", proj_orientation, false);

    auto conjure = resolve_conjure_spell_vfx_step(spells, row, label, "conjure", conj_anim);
    if (conjure) {
        conjure->caster_animation = map_conjure_animation(conj_anim);
        if (conjure->debug_origin.empty()) {
            conjure->debug_origin = format_debug_origin("spells", row, "conjure");
        }
    }
    const int conjure_duration_ms = get_spell_time_ms(spells, row, "ConjTime").value_or(1500);
    const int conjure_step_duration_ms = conjure
        ? estimate_burst_step_duration_ms(conjure->source, conjure_duration_ms)
        : conjure_duration_ms;
    append_sequence_step(sequence, std::move(conjure), conjure_step_duration_ms);

    auto cast = resolve_spell_vfx_step(spells, row, label, "cast", {
            {"CastHandVisual", "handconjure"},
            {"CastHeadVisual", "headconjure"},
            {"CastGrndVisual", "rootdummy"},
        });
    if (cast) {
        cast->caster_animation = map_cast_animation(cast_anim);
        if (cast->debug_origin.empty()) {
            cast->debug_origin = format_debug_origin("spells", row, "cast");
        }
    }
    const int cast_duration_ms = get_spell_time_ms(spells, row, "CastTime").value_or(1000);
    const int cast_step_duration_ms = cast
        ? estimate_burst_step_duration_ms(cast->source, cast_duration_ms)
        : cast_duration_ms;
    append_sequence_step(sequence, std::move(cast), cast_step_duration_ms);

    if (auto projectile = resolve_model_from_2da_row(spells, row, label, {"ProjModel"})) {
        nw::String proj_type;
        spells.get_to(row, "ProjType", proj_type, false);
        const int projectile_duration_ms = estimate_spell_projectile_duration_ms(
            spells, row, sequence.source_target_distance, *projectile);
        VfxSequenceStep step;
        step.label = "projectile";
        step.source = std::move(*projectile);
        step.kind = VfxSequenceStepKind::projectile;
        step.duration_ms = projectile_duration_ms;
        step.anchor = map_projectile_anchor(proj_spawn_point);
        step.source_anchor = preferred_effect_source_anchor(step.source);
        step.caster_animation = map_cast_animation(cast_anim);
        step.scale = 1.0f;
        step.target_side = true;
        step.target_point_kind = VfxTargetPointKind::anchor;
        step.target_anchor = std::string(kSpellSequenceTargetAnchor);
        step.projectile_path = spell_projectile_path(proj_type);
        step.projectile_orientation = spell_projectile_orientation(proj_orientation);
        step.debug_origin = format_debug_origin("spells", row, "ProjModel");
        sequence.steps.push_back(std::move(step));
    } else {
        nw::String impact_script;
        spells.get_to(row, "ImpactScript", impact_script, false);
        const auto compatibility = resolve_stock_spell_projectile_fx(impact_script);
        auto step = compatibility
            ? resolve_scripted_spell_projectile_step(spells, row, label, sequence.source_target_distance, *compatibility)
            : std::nullopt;
        if (step) {
            const int projectile_start_ms = sequence_end_ms(sequence);
            const uint32_t projectile_count = compatibility->projectile_count;
            const int projectile_cadence_ms = compatibility->cadence_ms;
            const auto orientation = spell_projectile_orientation(proj_orientation);
            for (uint32_t projectile_index = 0; projectile_index < projectile_count; ++projectile_index) {
                auto projectile_step = *step;
                projectile_step.anchor = map_projectile_anchor(proj_spawn_point);
                projectile_step.caster_animation = map_cast_animation(cast_anim);
                projectile_step.start_offset_ms = projectile_start_ms
                    + static_cast<int>(projectile_index) * projectile_cadence_ms;
                projectile_step.target_point_kind = VfxTargetPointKind::anchor;
                projectile_step.target_anchor = std::string(kSpellSequenceTargetAnchor);
                if (orientation != VfxProjectileOrientationKind::none) {
                    projectile_step.projectile_orientation = orientation;
                }
                if (projectile_count > 1) {
                    projectile_step.label = "projectile " + std::to_string(projectile_index + 1);
                    projectile_step.debug_origin += " [" + std::to_string(projectile_index + 1) + "/"
                        + std::to_string(projectile_count) + "]";
                }
                sequence.steps.push_back(std::move(projectile_step));
            }
        }
    }

    if (sequence.steps.size() < 2) {
        return std::nullopt;
    }

    LOG_F(INFO, "Resolved VFX spell sequence '{}' with {} steps", sequence.label, sequence.steps.size());
    return sequence;
}

std::optional<VfxSequence> resolve_vfx_sequence_from_spell(std::string_view query)
{
    const auto* spells = nw::kernel::twodas().get("spells");
    if (!spells) {
        return std::nullopt;
    }

    const auto row = find_2da_row_by_label(*spells, query);
    if (!row) {
        return std::nullopt;
    }

    return build_spell_sequence(*spells, *row);
}

std::optional<VfxSequence> resolve_vfx_sequence_from_visualeffect(std::string_view query)
{
    const auto* visualeffects = nw::kernel::twodas().get("visualeffects");
    if (!visualeffects) {
        return std::nullopt;
    }

    auto row = find_2da_row_by_label(*visualeffects, query);
    if (!row) {
        row = parse_2da_row_index(query, *visualeffects);
    }
    if (!row) {
        return std::nullopt;
    }

    return build_visualeffect_sequence(*visualeffects, *row);
}

std::optional<std::string> resolve_visualeffect_vfx_source(std::string_view query, VfxLookupStage stage)
{
    const auto* visualeffects = nw::kernel::twodas().get("visualeffects");
    if (!visualeffects) {
        return std::nullopt;
    }

    auto row = find_2da_row_by_label(*visualeffects, query);
    if (!row) {
        row = parse_2da_row_index(query, *visualeffects);
    }
    if (!row) {
        return std::nullopt;
    }

    nw::String label;
    visualeffects->get_to(*row, "Label", label, false);

    switch (stage) {
    case VfxLookupStage::projectile:
    case VfxLookupStage::cast:
    case VfxLookupStage::conjure:
        return std::nullopt;
    case VfxLookupStage::impact:
        if (auto resolved = resolve_model_from_2da_row(*visualeffects, *row, label, {
            "Imp_HeadCon_Node",
            "Imp_Impact_Node",
            "Imp_Root_M_Node",
            "Imp_Root_L_Node",
            "Imp_Root_H_Node",
            "Imp_Root_S_Node",
            "LowQuality",
            "LowViolence",
        })) {
            return resolved;
        }
        return resolve_visualeffect_progfx_source(*visualeffects, *row, label, {"ProgFX_Impact"});
    case VfxLookupStage::duration:
        if (auto resolved = resolve_model_from_2da_row(*visualeffects, *row, label, {
            "Imp_HeadCon_Node",
            "Imp_Impact_Node",
            "Imp_Root_M_Node",
            "Imp_Root_L_Node",
            "Imp_Root_H_Node",
            "Imp_Root_S_Node",
            "LowQuality",
            "LowViolence",
        })) {
            return resolved;
        }
        return resolve_visualeffect_progfx_source(*visualeffects, *row, label, {"ProgFX_Duration"});
    case VfxLookupStage::cessation:
        if (auto resolved = resolve_model_from_2da_row(*visualeffects, *row, label, {
            "Ces_HeadCon_Node",
            "Ces_Impact_Node",
            "Ces_Root_M_Node",
            "Ces_Root_L_Node",
            "Ces_Root_H_Node",
            "Ces_Root_S_Node",
        })) {
            return resolved;
        }
        return resolve_visualeffect_progfx_source(*visualeffects, *row, label, {"ProgFX_Cessation"});
    case VfxLookupStage::auto_select:
        if (auto resolved = resolve_model_from_2da_row(*visualeffects, *row, label, {
            "Imp_HeadCon_Node",
            "Imp_Impact_Node",
            "Imp_Root_M_Node",
            "Imp_Root_L_Node",
            "Imp_Root_H_Node",
            "Imp_Root_S_Node",
            "LowQuality",
            "LowViolence",
        })) {
            return resolved;
        }
        if (auto resolved = resolve_visualeffect_progfx_source(*visualeffects, *row, label, {"ProgFX_Impact"})) {
            return resolved;
        }
        if (auto resolved = resolve_visualeffect_progfx_source(*visualeffects, *row, label, {"ProgFX_Duration"})) {
            return resolved;
        }
        return resolve_visualeffect_progfx_source(*visualeffects, *row, label, {"ProgFX_Cessation"});
    }

    return std::nullopt;
}

bool run_nwn_animation_smoke_case(const NwnAnimationSmokeCase& smoke_case)
{
    auto* mdl = nw::kernel::models().load(smoke_case.resref);
    if (!mdl) {
        LOG_F(ERROR, "NWN animation smoke failed to load {} ({})",
            smoke_case.resref, smoke_case.description);
        return false;
    }

    std::vector<int32_t> joint_to_source_node;
    auto skeleton = build_nwn_skeleton(*mdl, joint_to_source_node);
    if (skeleton.joints.empty()) {
        LOG_F(ERROR, "NWN animation smoke found no skeleton nodes for {} ({})",
            smoke_case.resref, smoke_case.description);
        return false;
    }

    const auto* animation = find_nwn_smoke_animation(*mdl);
    if (!animation) {
        LOG_F(ERROR, "NWN animation smoke found no idle animation for {} ({})",
            smoke_case.resref, smoke_case.description);
        return false;
    }

    auto backend = nw::render::make_animation_backend(nw::render::AnimationBackendKind::ozz);
    if (!backend || !backend->build_skeleton(0, skeleton)) {
        LOG_F(ERROR, "NWN animation smoke failed to build ozz skeleton for {}", smoke_case.resref);
        return false;
    }

    auto clip = build_nwn_clip(*animation, skeleton, 0);
    if (!backend->build_clip(0, clip)) {
        LOG_F(ERROR, "NWN animation smoke failed to build ozz clip {} for {}",
            animation->name, smoke_case.resref);
        return false;
    }

    nw::render::Pose pose0;
    nw::render::Pose pose1;
    if (!backend->sample(0, 0.0f, pose0, true) || !backend->sample(0, 0.25f, pose1, true)) {
        LOG_F(ERROR, "NWN animation smoke failed to sample ozz clip {} for {}",
            animation->name, smoke_case.resref);
        return false;
    }
    if (pose0.local.size() != joint_to_source_node.size() || pose1.local.size() != joint_to_source_node.size()) {
        LOG_F(ERROR, "NWN animation smoke got unexpected pose size for {}", smoke_case.resref);
        return false;
    }

    nw::render::build_model_matrices(skeleton, pose1);
    if (pose1.model.size() != joint_to_source_node.size()) {
        LOG_F(ERROR, "NWN animation smoke got unexpected model matrix count for {}", smoke_case.resref);
        return false;
    }

    LOG_F(INFO, "NWN animation smoke passed for {} ({}) using animation {}",
        smoke_case.resref, smoke_case.description, animation->name);
    return true;
}

std::vector<std::string> detect_texture_kinds(std::string_view name)
{
    std::vector<std::string> result;
    if (name.empty() || name == "null") {
        return result;
    }

    auto& resman = nw::kernel::resman();
    if (resman.contains({name, nw::ResourceType::plt})) {
        result.push_back("plt");
    }
    if (resman.contains({name, nw::ResourceType::dds})) {
        result.push_back("dds");
    }
    if (resman.contains({name, nw::ResourceType::tga})) {
        result.push_back("tga");
    }
    if (resman.contains({name, nw::ResourceType::txi})) {
        result.push_back("txi");
    }
    if (resman.contains({name, nw::ResourceType::shd})) {
        result.push_back("shd");
    }
    return result;
}

std::string describe_tga_texture(std::string_view name)
{
    static constexpr uint8_t top_origin_mask = 0x20;
    static constexpr uint8_t right_origin_mask = 0x10;
    static constexpr size_t descriptor_offset = 17;

    if (!nw::kernel::resman().contains({name, nw::ResourceType::tga})) {
        return {};
    }

    auto data = nw::kernel::resman().demand({name, nw::ResourceType::tga});
    if (data.bytes.size() <= descriptor_offset) {
        return "tga-descriptor=<missing>";
    }

    const auto descriptor = data.bytes[descriptor_offset];
    std::ostringstream out;
    out << "tga-origin=" << ((descriptor & top_origin_mask) ? "top-left" : "bottom-left");
    if (descriptor & right_origin_mask) {
        out << " right-to-left";
    }
    out << " descriptor=0x"
        << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(descriptor);
    return out.str();
}

std::string join_strings(const std::vector<std::string>& values, std::string_view sep)
{
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << sep;
        }
        out << values[i];
    }
    return out.str();
}

std::string mesh_texture_name(const nw::model::TrimeshNode& node)
{
    if (!node.bitmap.empty() && node.bitmap != "null") {
        return std::string(node.bitmap);
    }
    if (!node.textures[0].empty() && node.textures[0] != "null") {
        return std::string(node.textures[0]);
    }
    return {};
}

std::string resource_key(const nw::Resource& resource)
{
    return resource.resref.string() + "." + std::string(nw::ResourceType::to_string(resource.type));
}

bool is_null_resource_name(std::string_view name)
{
    return name.empty()
        || nw::string::icmp(name, "null")
        || nw::string::icmp(name, "none")
        || nw::string::icmp(name, "****");
}

std::string clean_resource_token(std::string_view token)
{
    while (!token.empty() && (token.front() == '"' || token.front() == '\'')) {
        token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == '"' || token.back() == '\'' || token.back() == ',' || token.back() == ';')) {
        token.remove_suffix(1);
    }

    std::string cleaned{token};
    std::replace(cleaned.begin(), cleaned.end(), '\\', '/');
    auto slash = cleaned.find_last_of('/');
    if (slash != std::string::npos) {
        cleaned = cleaned.substr(slash + 1);
    }
    return cleaned;
}

bool looks_like_resref_token(std::string_view token)
{
    if (token.empty() || token.size() > 64) {
        return false;
    }

    bool has_alpha = false;
    for (char ch : token) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalpha(uch)) {
            has_alpha = true;
        }
        if (!std::isalnum(uch) && ch != '_' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return has_alpha;
}

bool resource_exists(const nw::Resource& resource)
{
    return nw::kernel::resman().contains(resource);
}

void add_dump_resource(DumpReport& report, nw::Resource resource, std::string origin)
{
    if (!resource.valid()) {
        return;
    }

    auto key = resource_key(resource);
    auto [it, inserted] = report.resources.emplace(key, DumpResource{});
    if (inserted) {
        it->second.resource = resource;
    }
    it->second.origins.insert(std::move(origin));

    if (resource.type == nw::ResourceType::txi || resource.type == nw::ResourceType::mtr) {
        report.scan_queue.push_back(resource);
    }
}

void add_existing_or_missing_resource(DumpReport& report, nw::Resource resource, std::string origin)
{
    if (!resource.valid()) {
        return;
    }

    add_dump_resource(report, resource, std::move(origin));
    if (!resource_exists(resource)) {
        report.resources[resource_key(resource)].missing = true;
    }
}

void add_texture_family(DumpReport& report, std::string_view raw_name, std::string origin)
{
    const auto cleaned = clean_resource_token(raw_name);
    if (is_null_resource_name(cleaned) || !looks_like_resref_token(cleaned)) {
        return;
    }

    const std::filesystem::path path{cleaned};
    const auto ext = path.extension().string();
    if (!ext.empty()) {
        auto type = nw::ResourceType::from_extension(ext);
        const auto stem = path.stem().string();
        if (type != nw::ResourceType::invalid && !is_null_resource_name(stem)) {
            add_existing_or_missing_resource(report, {stem, type}, std::move(origin));
        }
        return;
    }

    static constexpr std::array texture_types{
        nw::ResourceType::dds,
        nw::ResourceType::tga,
        nw::ResourceType::plt,
    };

    bool found_texture = false;
    for (auto type : texture_types) {
        nw::Resource texture{cleaned, type};
        if (resource_exists(texture)) {
            add_dump_resource(report, texture, origin);
            found_texture = true;
        }
    }

    nw::Resource txi{cleaned, nw::ResourceType::txi};
    if (resource_exists(txi)) {
        add_dump_resource(report, txi, origin);
    }

    nw::Resource shd{cleaned, nw::ResourceType::shd};
    if (resource_exists(shd)) {
        add_dump_resource(report, shd, origin);
    }

    if (!found_texture && !resource_exists(txi)) {
        add_existing_or_missing_resource(report, {cleaned, nw::ResourceType::dds}, std::move(origin));
    }
}

void add_material_resource(DumpReport& report, std::string_view raw_name, std::string origin)
{
    const auto cleaned = clean_resource_token(raw_name);
    if (is_null_resource_name(cleaned) || !looks_like_resref_token(cleaned)) {
        return;
    }

    const std::filesystem::path path{cleaned};
    if (!path.extension().empty()) {
        const auto type = nw::ResourceType::from_extension(path.extension().string());
        if (type == nw::ResourceType::mtr) {
            add_existing_or_missing_resource(report, {path.stem().string(), type}, std::move(origin));
        }
        return;
    }

    nw::Resource material{cleaned, nw::ResourceType::mtr};
    if (resource_exists(material)) {
        add_dump_resource(report, material, std::move(origin));
    }
}

void add_existing_text_resource_reference(DumpReport& report, std::string_view raw_name, std::string origin)
{
    const auto cleaned = clean_resource_token(raw_name);
    if (is_null_resource_name(cleaned) || !looks_like_resref_token(cleaned)) {
        return;
    }

    const std::filesystem::path path{cleaned};
    if (!path.extension().empty()) {
        const auto type = nw::ResourceType::from_extension(path.extension().string());
        const auto stem = path.stem().string();
        nw::Resource resource{stem, type};
        if (resource.valid() && resource_exists(resource)) {
            add_dump_resource(report, resource, std::move(origin));
        }
        return;
    }

    static constexpr std::array resource_types{
        nw::ResourceType::dds,
        nw::ResourceType::tga,
        nw::ResourceType::plt,
        nw::ResourceType::txi,
        nw::ResourceType::mtr,
        nw::ResourceType::shd,
    };
    for (auto type : resource_types) {
        nw::Resource resource{cleaned, type};
        if (resource_exists(resource)) {
            add_dump_resource(report, resource, origin);
        }
    }
}

std::vector<std::string> tokenize_dependency_text(std::string_view text)
{
    std::vector<std::string> tokens;
    std::string current;
    bool in_comment = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_comment) {
            if (ch == '\n' || ch == '\r') {
                in_comment = false;
            }
            continue;
        }
        if (ch == '#' || (ch == '/' && i + 1 < text.size() && text[i + 1] == '/')) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            in_comment = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

bool dependency_key_may_reference_texture(std::string_view key)
{
    std::string lowered{key};
    nw::string::tolower(&lowered);
    return lowered.find("texture") != std::string::npos
        || lowered.find("map") != std::string::npos
        || lowered.find("normal") != std::string::npos
        || lowered.find("bump") != std::string::npos
        || lowered.find("rough") != std::string::npos
        || lowered.find("metal") != std::string::npos
        || lowered.find("spec") != std::string::npos
        || lowered.find("height") != std::string::npos
        || lowered.find("emissive") != std::string::npos;
}

void scan_text_dependency_resource(DumpReport& report, const nw::Resource& resource)
{
    const auto key = resource_key(resource);
    if (report.scanned_text_resources.contains(key) || !resource_exists(resource)) {
        return;
    }
    report.scanned_text_resources.insert(key);

    auto data = nw::kernel::resman().demand(resource);
    const auto tokens = tokenize_dependency_text(data.bytes.string_view());
    for (const auto& token : tokens) {
        add_existing_text_resource_reference(report, token, key);
    }
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const auto& key_token = tokens[i];
        const auto& value_token = tokens[i + 1];
        if (dependency_key_may_reference_texture(key_token)) {
            add_texture_family(report, value_token, key);
        }
        if (nw::string::icmp(key_token, "material")
            || nw::string::icmp(key_token, "materialname")) {
            add_material_resource(report, value_token, key);
        }
    }
}

void scan_node_dump_dependencies(DumpReport& report, const nw::model::Node* node, std::string_view model_name)
{
    if (!node) {
        return;
    }

    const auto origin = std::string(model_name) + ":" + std::string(node->name);
    if (auto trimesh = dynamic_cast<const nw::model::TrimeshNode*>(node)) {
        add_texture_family(report, mesh_texture_name(*trimesh), origin);
        for (const auto& texture : trimesh->textures) {
            add_texture_family(report, texture, origin);
        }
        add_material_resource(report, trimesh->materialname, origin);
    } else if (auto emitter = dynamic_cast<const nw::model::EmitterNode*>(node)) {
        add_texture_family(report, emitter->texture, origin);
    } else if (auto light = dynamic_cast<const nw::model::LightNode*>(node)) {
        for (const auto& texture : light->textures) {
            add_texture_family(report, texture, origin);
        }
    } else if (auto reference = dynamic_cast<const nw::model::ReferenceNode*>(node)) {
        if (!is_null_resource_name(reference->refmodel)) {
            add_existing_or_missing_resource(report, {reference->refmodel, nw::ResourceType::mdl}, origin);
        }
    }

    for (const auto* child : node->children) {
        scan_node_dump_dependencies(report, child, model_name);
    }
}

void scan_model_dump_dependencies(DumpReport& report, std::string_view resref, bool scan_visual_assets = true)
{
    if (is_null_resource_name(resref)) {
        return;
    }

    const std::string model_name{resref};
    const bool first_model_scan = report.scanned_models.insert(model_name).second;
    add_existing_or_missing_resource(report, {resref, nw::ResourceType::mdl}, "model");

    if (!first_model_scan && (!scan_visual_assets || report.scanned_model_visuals.contains(model_name))) {
        return;
    }

    auto* mdl = nw::kernel::models().load(resref);
    if (!mdl || !mdl->valid()) {
        report.resources[resource_key({resref, nw::ResourceType::mdl})].missing = true;
        return;
    }

    if (first_model_scan && !is_null_resource_name(mdl->model.supermodel_name)) {
        scan_model_dump_dependencies(report, mdl->model.supermodel_name, false);
    }

    if (!scan_visual_assets || !report.scanned_model_visuals.insert(model_name).second) {
        return;
    }

    for (const auto& node : mdl->model.nodes) {
        if (node && !node->parent) {
            scan_node_dump_dependencies(report, node.get(), resref);
        }
    }

    std::vector<std::string> referenced_models;
    for (const auto& [_, item] : report.resources) {
        if (item.resource.type == nw::ResourceType::mdl && !item.missing) {
            const auto name = item.resource.resref.string();
            if (!report.scanned_models.contains(name)) {
                referenced_models.push_back(name);
            }
        }
    }
    for (const auto& name : referenced_models) {
        scan_model_dump_dependencies(report, name);
    }
}

std::filesystem::path dump_output_root(std::string_view resref, const std::filesystem::path& output_dir)
{
    if (output_dir == "." || output_dir.empty()) {
        return std::filesystem::path{"dump"} / std::string(resref);
    }
    return output_dir;
}

bool write_text_file(const std::filesystem::path& path, std::string_view text)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LOG_F(ERROR, "Failed to create output directory '{}': {}", path.parent_path().string(), ec.message());
        return false;
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

std::filesystem::path dump_resource_path(const std::filesystem::path& root, const nw::Resource& resource)
{
    return root / std::string(nw::ResourceType::to_string(resource.type)) / resource.filename();
}

json dump_manifest_json(std::string_view root_resref, const DumpReport& report)
{
    json resources = json::array();
    size_t missing_count = 0;
    size_t copied_count = 0;
    for (const auto& [_, item] : report.resources) {
        json origins = json::array();
        for (const auto& origin : item.origins) {
            origins.push_back(origin);
        }
        resources.push_back({
            {"resource", item.resource.filename()},
            {"resref", item.resource.resref.string()},
            {"type", std::string(nw::ResourceType::to_string(item.resource.type))},
            {"status", item.missing ? "missing" : (item.copied ? "copied" : "present")},
            {"bytes", item.bytes},
            {"origins", origins},
        });
        missing_count += item.missing ? 1u : 0u;
        copied_count += item.copied ? 1u : 0u;
    }

    return {
        {"root", std::string(root_resref)},
        {"resource_count", report.resources.size()},
        {"copied_count", copied_count},
        {"missing_count", missing_count},
        {"resources", resources},
    };
}

size_t mesh_vertex_count(const nw::model::TrimeshNode& node)
{
    if (auto skin = dynamic_cast<const nw::model::SkinNode*>(&node)) {
        return skin->vertices.size();
    }
    return node.vertices.size();
}

size_t skin_bone_count(const nw::model::SkinNode& node)
{
    size_t count = 0;
    for (auto bone : node.bone_nodes) {
        if (bone > 0) {
            ++count;
        }
    }
    return count;
}

template <typename TVertex>
std::optional<std::pair<glm::vec2, glm::vec2>> vertex_uv_bounds(const std::vector<TVertex>& vertices)
{
    if (vertices.empty()) {
        return std::nullopt;
    }

    glm::vec2 min_uv = vertices.front().tex_coords;
    glm::vec2 max_uv = vertices.front().tex_coords;
    for (const auto& vertex : vertices) {
        min_uv = glm::min(min_uv, vertex.tex_coords);
        max_uv = glm::max(max_uv, vertex.tex_coords);
    }
    return std::pair{min_uv, max_uv};
}

std::optional<std::pair<glm::vec2, glm::vec2>> mesh_uv_bounds(const nw::model::TrimeshNode& node)
{
    if (auto skin = dynamic_cast<const nw::model::SkinNode*>(&node)) {
        return vertex_uv_bounds(skin->vertices);
    }
    return vertex_uv_bounds(node.vertices);
}

std::optional<float> scalar_controller(const nw::model::Node& node, uint32_t type)
{
    const auto value = node.get_controller(type);
    if (value.key && !value.data.empty()) {
        return value.data[0];
    }
    return std::nullopt;
}

std::optional<glm::vec3> vec3_controller(const nw::model::Node& node, uint32_t type)
{
    const auto value = node.get_controller(type);
    if (value.key && value.data.size() >= 3) {
        return glm::vec3{value.data[0], value.data[1], value.data[2]};
    }
    return std::nullopt;
}

void collect_light_stats(const nw::model::Node* node, std::vector<LightNodeStats>& lights)
{
    if (!node) {
        return;
    }

    if (const auto* light = dynamic_cast<const nw::model::LightNode*>(node)) {
        lights.push_back(LightNodeStats{
            .node = light,
            .controller_color = vec3_controller(*light, nw::model::ControllerType::Color),
            .controller_radius = scalar_controller(*light, nw::model::ControllerType::Radius),
            .controller_shadow_radius = scalar_controller(*light, nw::model::ControllerType::ShadowRadius),
            .controller_multiplier = scalar_controller(*light, nw::model::ControllerType::Multiplier),
        });
    }

    for (const auto* child : node->children) {
        collect_light_stats(child, lights);
    }
}

void print_light_stats(const std::vector<LightNodeStats>& lights)
{
    std::cout << "lights:\n";
    if (lights.empty()) {
        std::cout << "  <none>\n";
        return;
    }

    for (const auto& light : lights) {
        if (!light.node) {
            continue;
        }

        std::cout << "  " << light.node->name
                  << " color=(" << light.node->color.x << ", " << light.node->color.y << ", " << light.node->color.z << ")";
        if (light.controller_color) {
            std::cout << " controller_color=(" << light.controller_color->x << ", "
                      << light.controller_color->y << ", " << light.controller_color->z << ")";
        }
        std::cout << " radius=" << light.node->flareradius;
        if (light.controller_radius) {
            std::cout << " controller_radius=" << *light.controller_radius;
        }
        if (light.controller_shadow_radius) {
            std::cout << " controller_shadow_radius=" << *light.controller_shadow_radius;
        }
        std::cout << " multiplier=" << light.node->multiplier;
        if (light.controller_multiplier) {
            std::cout << " controller_multiplier=" << *light.controller_multiplier;
        }
        std::cout << " ambientonly=" << light.node->ambientonly
                  << " dynamic=" << (light.node->dynamic ? 1 : 0)
                  << " affectdynamic=" << light.node->affectdynamic
                  << " shadow=" << light.node->shadow
                  << " fading=" << light.node->fadinglight
                  << '\n';
    }
}

TileLightSlots area_tile_light_slots(const nw::AreaTile& tile) noexcept
{
    return TileLightSlots{
        .main1 = tile.mainlight1,
        .main2 = tile.mainlight2,
        .source1 = tile.srclight1,
        .source2 = tile.srclight2,
    };
}

float color_max_channel(const glm::vec3& color) noexcept
{
    return std::max(color.x, std::max(color.y, color.z));
}

glm::vec3 tile_light_hue(uint8_t index)
{
    static const std::array<glm::vec3, 7> colors{
        glm::vec3{1.0f, 0.82f, 0.32f},
        glm::vec3{0.32f, 1.0f, 0.36f},
        glm::vec3{0.25f, 0.95f, 1.0f},
        glm::vec3{0.34f, 0.52f, 1.0f},
        glm::vec3{0.72f, 0.38f, 1.0f},
        glm::vec3{1.0f, 0.32f, 0.28f},
        glm::vec3{1.0f, 0.58f, 0.22f},
    };
    return colors[std::min<size_t>(index, colors.size() - 1)];
}

glm::vec3 tile_main_light_color(uint8_t color_id)
{
    static constexpr std::array<float, 4> white_strengths{0.0f, 0.45f, 0.76f, 1.0f};
    static constexpr std::array<float, 4> hue_strengths{0.36f, 0.56f, 0.78f, 1.0f};
    if (color_id <= 3) {
        return glm::vec3{white_strengths[color_id]};
    }

    const uint8_t hue_index = static_cast<uint8_t>((color_id - 4) / 4);
    const uint8_t strength_index = static_cast<uint8_t>((color_id - 4) % 4);
    return tile_light_hue(hue_index) * hue_strengths[strength_index];
}

bool has_visible_light_color(const glm::vec3& color) noexcept
{
    return color_max_channel(color) > 1.0e-4f;
}

float light_authored_radius(const nw::model::LightNode& light)
{
    return scalar_controller(light, nw::model::ControllerType::Radius)
        .value_or(scalar_controller(light, nw::model::ControllerType::ShadowRadius).value_or(light.flareradius));
}

glm::vec3 model_light_authored_color(const nw::model::LightNode& light)
{
    if (auto authored_color = vec3_controller(light, nw::model::ControllerType::Color)) {
        if (has_visible_light_color(*authored_color)) {
            return *authored_color;
        }
    }
    if (has_visible_light_color(light.color)) {
        return light.color;
    }
    return glm::vec3{0.0f};
}

glm::vec3 area_model_light_color(const nw::model::LightNode& light, const TileLightSlots& slots)
{
    glm::vec3 color{0.0f};
    if (has_tile_light_slots(slots)) {
        color = tile_slot_color_for_model_light(light, slots);
    }
    if (!has_visible_light_color(color)) {
        color = model_light_authored_color(light);
    }
    return glm::clamp(color, glm::vec3{0.0f}, glm::vec3{1.0f});
}

glm::vec3 placeable_table_light_color(const nw::PlaceableInfo& info) noexcept
{
    if (!info.has_light()) {
        return glm::vec3{0.0f};
    }

    return tile_main_light_color(static_cast<uint8_t>(std::clamp(info.light_color, 0, 31)));
}

glm::mat4 area_object_placement_transform(const nw::Location& location)
{
    constexpr float k_epsilon = 1.0e-5f;
    float angle = 0.0f;
    if (std::abs(location.orientation.x) > k_epsilon || std::abs(location.orientation.y) > k_epsilon) {
        angle = std::atan2(location.orientation.y, location.orientation.x);
    }

    glm::mat4 placement = glm::translate(glm::mat4{1.0f}, location.position);
    placement *= glm::toMat4(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));
    return placement;
}

glm::quat model_quat_controller(
    const nw::model::Node& node,
    uint32_t type,
    const glm::quat& fallback = glm::quat{1.0f, 0.0f, 0.0f, 0.0f})
{
    const auto value = node.get_controller(type);
    if (value.key && value.data.size() >= 4) {
        const glm::quat result{value.data[3], value.data[0], value.data[1], value.data[2]};
        if (glm::dot(result, result) >= 1.0e-12f) {
            return glm::normalize(result);
        }
    }
    return fallback;
}

glm::mat4 model_node_local_transform(const nw::model::Node& node)
{
    glm::mat4 result = glm::translate(
        glm::mat4{1.0f},
        vec3_controller(node, nw::model::ControllerType::Position).value_or(glm::vec3{0.0f}));
    result *= glm::mat4_cast(model_quat_controller(node, nw::model::ControllerType::Orientation));
    result = glm::scale(
        result,
        vec3_controller(node, nw::model::ControllerType::Scale).value_or(glm::vec3{1.0f}));
    return result;
}

glm::mat4 model_node_transform(const nw::model::Node& node)
{
    glm::mat4 result = model_node_local_transform(node);
    for (auto* parent = node.parent; parent; parent = parent->parent) {
        result = model_node_local_transform(*parent) * result;
    }
    return result;
}

AreaLightTuning area_light_tuning(const nw::Area& area) noexcept
{
    const bool interior = (area.flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool underground = (area.flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    const bool night = area.weather.is_night != 0 || underground;

    if (interior) {
        return AreaLightTuning{.radius_scale = 0.82f, .intensity_scale = 0.52f};
    }
    if (underground) {
        return AreaLightTuning{.radius_scale = 0.82f, .intensity_scale = 0.52f};
    }
    if (night) {
        return AreaLightTuning{.radius_scale = 0.80f, .intensity_scale = 0.50f};
    }
    return AreaLightTuning{.radius_scale = 0.62f, .intensity_scale = 0.28f};
}

float area_model_light_radius(const nw::model::LightNode& light, const TileLightSlots& slots, const AreaLightTuning& tuning)
{
    const bool has_slots = has_tile_light_slots(slots);
    const bool uses_main_slot = model_light_is_main_tile_slot(light);
    const float minimum_radius = has_slots ? (uses_main_slot ? 6.0f : 3.4f) : 2.6f;
    const float maximum_radius = has_slots ? (uses_main_slot ? 9.2f : 5.4f) : 4.8f;
    return std::clamp(light_authored_radius(light), minimum_radius, maximum_radius) * tuning.radius_scale;
}

float area_model_light_intensity(const nw::model::LightNode& light, const TileLightSlots& slots, const AreaLightTuning& tuning)
{
    const bool has_slots = has_tile_light_slots(slots);
    const bool uses_main_slot = model_light_is_main_tile_slot(light);
    const float minimum_intensity = has_slots ? (uses_main_slot ? 0.30f : 0.46f) : 0.30f;
    const float maximum_intensity = has_slots ? (uses_main_slot ? 1.0f : 1.35f) : 1.0f;
    const float contribution_scale = has_slots && uses_main_slot ? 0.45f : 1.0f;
    const float multiplier = scalar_controller(light, nw::model::ControllerType::Multiplier)
        .value_or(light.multiplier > 0.0f ? light.multiplier : 1.0f);
    return std::clamp(std::max(multiplier, minimum_intensity), 0.0f, maximum_intensity)
        * tuning.intensity_scale * contribution_scale;
}

std::string light_slots_string(const TileLightSlots& slots)
{
    std::ostringstream ss;
    ss << static_cast<int>(slots.main1) << ','
       << static_cast<int>(slots.main2) << ','
       << static_cast<int>(slots.source1) << ','
       << static_cast<int>(slots.source2);
    return ss.str();
}

void print_vec3(const glm::vec3& value)
{
    std::cout << value.x << ", " << value.y << ", " << value.z;
}

void collect_node_stats(const nw::model::Node* node, size_t depth, StatsReport& report)
{
    if (!node) {
        return;
    }

    ++report.total_nodes;
    report.max_depth = std::max(report.max_depth, depth);
    ++report.node_type_counts[std::string(nw::model::NodeType::to_string(node->type))];

    switch (node->type) {
    case nw::model::NodeType::skin:
        ++report.total_skins;
        break;
    case nw::model::NodeType::animmesh:
        ++report.total_animmesh;
        break;
    case nw::model::NodeType::danglymesh:
        ++report.total_danglymesh;
        break;
    case nw::model::NodeType::aabb:
        ++report.total_aabb;
        break;
    case nw::model::NodeType::emitter:
        ++report.total_emitters;
        break;
    case nw::model::NodeType::light:
        ++report.total_lights;
        break;
    case nw::model::NodeType::reference:
        ++report.total_references;
        break;
    default:
        break;
    }

    if (auto trimesh = dynamic_cast<const nw::model::TrimeshNode*>(node)) {
        ++report.total_meshes;
        report.total_vertices += mesh_vertex_count(*trimesh);
        report.total_indices += trimesh->indices.size();

        if (auto dangly = dynamic_cast<const nw::model::DanglymeshNode*>(trimesh)) {
            const auto policy = nw::render::nwn::dangly_deform_policy_for(dangly);
            ++report.dangly_policy_counts[std::string(nw::render::nwn::dangly_deform_policy_name(policy))];
        }

        if (auto skin = dynamic_cast<const nw::model::SkinNode*>(trimesh)) {
            auto bones = skin_bone_count(*skin);
            report.total_skin_bones += bones;
            report.max_skin_bones = std::max(report.max_skin_bones, bones);
        }

        auto texture_name = mesh_texture_name(*trimesh);
        if (!texture_name.empty()) {
            auto& tex = report.textures[texture_name];
            tex.name = texture_name;
            if (tex.kinds.empty()) {
                tex.kinds = detect_texture_kinds(texture_name);
                const bool has_tga = std::find(tex.kinds.begin(), tex.kinds.end(), "tga") != tex.kinds.end();
                const bool has_dds = std::find(tex.kinds.begin(), tex.kinds.end(), "dds") != tex.kinds.end();
                const bool has_plt = std::find(tex.kinds.begin(), tex.kinds.end(), "plt") != tex.kinds.end();
                if (has_tga && !has_dds && !has_plt) {
                    tex.detail = describe_tga_texture(texture_name);
                }
            }
        }
    }

    for (const auto* child : node->children) {
        collect_node_stats(child, depth + 1, report);
    }
}

void print_node_tree(const nw::model::Node* node, size_t depth)
{
    if (!node) {
        return;
    }

    std::cout << std::string(depth * 2, ' ')
              << "- " << node->name
              << " [" << nw::model::NodeType::to_string(node->type) << "]";

    if (auto trimesh = dynamic_cast<const nw::model::TrimeshNode*>(node)) {
        std::cout << " vertices=" << mesh_vertex_count(*trimesh)
                  << " indices=" << trimesh->indices.size();
        if (auto skin = dynamic_cast<const nw::model::SkinNode*>(trimesh)) {
            std::cout << " bones=" << skin_bone_count(*skin);
        }
        if (auto dangly = dynamic_cast<const nw::model::DanglymeshNode*>(trimesh)) {
            const auto policy = nw::render::nwn::dangly_deform_policy_for(dangly);
            std::cout << " dangly_policy=" << nw::render::nwn::dangly_deform_policy_name(policy)
                      << " constraints=" << dangly->constraints.size();
        }
        auto texture_name = mesh_texture_name(*trimesh);
        if (!texture_name.empty()) {
            auto kinds = detect_texture_kinds(texture_name);
            std::cout << " texture=" << texture_name;
            if (!kinds.empty()) {
                std::cout << " (" << join_strings(kinds, "/") << ")";
            }
        }
        if (!trimesh->render) {
            std::cout << " render=0";
        }
        if (!trimesh->renderhint.empty()) {
            std::cout << " renderhint=" << trimesh->renderhint;
        }
        if (!trimesh->materialname.empty()) {
            std::cout << " materialname=" << trimesh->materialname;
        }
        if (trimesh->transparencyhint != 0) {
            std::cout << " transparencyhint=" << trimesh->transparencyhint;
        }
        if (trimesh->tilefade != 0) {
            std::cout << " tilefade=" << trimesh->tilefade;
        }
        if (trimesh->rotatetexture) {
            std::cout << " rotatetexture=1";
        }
        if (trimesh->lightmapped != 0) {
            std::cout << " lightmapped=" << trimesh->lightmapped;
        }
        if (auto bounds = mesh_uv_bounds(*trimesh)) {
            std::cout << " uv=("
                      << bounds->first.x << ", " << bounds->first.y << ")..("
                      << bounds->second.x << ", " << bounds->second.y << ")";
        }
    }

    std::cout << '\n';
    for (const auto* child : node->children) {
        print_node_tree(child, depth + 1);
    }
}

} // namespace

int run_nwn_animation_smoke_command(std::string_view module_path, std::string_view user_path)
{
    if (!init_kernel_services(module_path, user_path)) {
        return 1;
    }

    static constexpr std::array<NwnAnimationSmokeCase, 2> smoke_cases{{
        {"c_bodak", "server asset supermodel animation path"},
        {"c_mindflayer", "server asset skinned animation path"},
    }};

    bool ok = true;
    for (const auto& smoke_case : smoke_cases) {
        ok = run_nwn_animation_smoke_case(smoke_case) && ok;
    }

    nw::kernel::services().shutdown();
    if (ok) {
        LOG_F(INFO, "NWN animation smoke succeeded");
        return 0;
    }
    return 1;
}

int run_dump_command(std::string_view resref, const std::filesystem::path& output_dir,
    std::string_view module_path, std::string_view user_path)
{
    if (resref.empty()) {
        print_usage();
        return 1;
    }

    if (!init_kernel_services(module_path, user_path)) {
        return 1;
    }

    auto shutdown = [] { nw::kernel::services().shutdown(); };
    DumpReport report;
    scan_model_dump_dependencies(report, resref);

    for (size_t i = 0; i < report.scan_queue.size(); ++i) {
        scan_text_dependency_resource(report, report.scan_queue[i]);
    }

    const auto root = dump_output_root(resref, output_dir);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        LOG_F(ERROR, "Failed to create dump directory '{}': {}", root.string(), ec.message());
        shutdown();
        return 1;
    }

    bool ok = true;
    for (auto& [_, item] : report.resources) {
        if (item.missing) {
            continue;
        }
        auto data = nw::kernel::resman().demand(item.resource);
        if (data.bytes.size() == 0) {
            item.missing = true;
            ok = false;
            continue;
        }

        const auto path = dump_resource_path(root, item.resource);
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            LOG_F(ERROR, "Failed to create resource dump directory '{}': {}", path.parent_path().string(), ec.message());
            ok = false;
            continue;
        }
        if (!data.bytes.write_to(path)) {
            LOG_F(ERROR, "Failed to write resource dump '{}'", path.string());
            ok = false;
            continue;
        }
        item.copied = true;
        item.bytes = data.bytes.size();
    }

    const auto manifest = dump_manifest_json(resref, report);
    if (!write_text_file(root / "manifest.json", manifest.dump(2))) {
        LOG_F(ERROR, "Failed to write dump manifest '{}'", (root / "manifest.json").string());
        ok = false;
    }

    size_t missing_count = 0;
    size_t copied_count = 0;
    for (const auto& [_, item] : report.resources) {
        missing_count += item.missing ? 1u : 0u;
        copied_count += item.copied ? 1u : 0u;
    }

    std::cout << "dump: " << resref << '\n';
    std::cout << "output: " << root.string() << '\n';
    std::cout << "resources: " << report.resources.size()
              << " copied=" << copied_count
              << " missing=" << missing_count << '\n';
    if (missing_count != 0) {
        std::cout << "missing:\n";
        for (const auto& [_, item] : report.resources) {
            if (item.missing) {
                std::cout << "  " << item.resource.filename() << '\n';
            }
        }
    }

    shutdown();
    return ok && missing_count == 0 ? 0 : 1;
}

int run_stats_command(std::string_view resref, std::string_view module_path, std::string_view user_path)
{
    if (resref.empty()) {
        print_usage();
        return 1;
    }

    if (!init_kernel_services(module_path, user_path)) {
        return 1;
    }

    auto shutdown = [] { nw::kernel::services().shutdown(); };

    auto* mdl = nw::kernel::models().load(resref);
    if (!mdl || !mdl->valid()) {
        LOG_F(ERROR, "Failed to load model: {}", resref);
        shutdown();
        return 1;
    }

    const auto& model = mdl->model;
    StatsReport report{};
    report.total_animations = model.animations.size();

    std::vector<const nw::model::Node*> roots;
    for (const auto& node : model.nodes) {
        if (node && !node->parent) {
            roots.push_back(node.get());
        }
    }

    for (const auto* root : roots) {
        collect_node_stats(root, 0, report);
    }
    std::vector<LightNodeStats> lights;
    for (const auto* root : roots) {
        collect_light_stats(root, lights);
    }

    std::cout << "model: " << resref << '\n';
    std::cout << "classification: " << static_cast<int>(model.classification) << '\n';
    std::cout << "supermodel: " << (model.supermodel_name.empty() ? "<none>" : std::string(model.supermodel_name)) << '\n';
    std::cout << "animations: " << report.total_animations << '\n';
    std::cout << "nodes: " << report.total_nodes << " (max depth " << report.max_depth << ")\n";
    std::cout << "meshes: " << report.total_meshes
              << " skin=" << report.total_skins
              << " animmesh=" << report.total_animmesh
              << " danglymesh=" << report.total_danglymesh
              << " aabb=" << report.total_aabb << '\n';
    if (report.total_danglymesh > 0) {
        std::cout << "dangly policies:";
        if (report.dangly_policy_counts.empty()) {
            std::cout << " <none>";
        } else {
            for (const auto& [policy, count] : report.dangly_policy_counts) {
                std::cout << ' ' << policy << '=' << count;
            }
        }
        std::cout << '\n';
    }
    if (report.total_skins > 0) {
        std::cout << "skinning: total bones=" << report.total_skin_bones
                  << " max bones per skin=" << report.max_skin_bones << '\n';
    }
    std::cout << "attachments: emitters=" << report.total_emitters
              << " lights=" << report.total_lights
              << " references=" << report.total_references << '\n';
    std::cout << "geometry: vertices=" << report.total_vertices
              << " indices=" << report.total_indices << '\n';
    std::cout << "bounds: bmin=(" << model.bmin.x << ", " << model.bmin.y << ", " << model.bmin.z
              << ") bmax=(" << model.bmax.x << ", " << model.bmax.y << ", " << model.bmax.z
              << ") radius=" << model.radius << '\n';

    std::cout << "node types:\n";
    for (const auto& [type, count] : report.node_type_counts) {
        std::cout << "  " << type << ": " << count << '\n';
    }

    std::cout << "textures:\n";
    if (report.textures.empty()) {
        std::cout << "  <none>\n";
    } else {
        for (const auto& [_, tex] : report.textures) {
            std::cout << "  " << tex.name << ": "
                      << (tex.kinds.empty() ? "missing" : join_strings(tex.kinds, "/"));
            if (!tex.detail.empty()) {
                std::cout << " " << tex.detail;
            }
            std::cout << '\n';
        }
    }

    print_light_stats(lights);

    std::cout << "hierarchy:\n";
    for (const auto* root : roots) {
        print_node_tree(root, 0);
    }

    shutdown();
    return 0;
}

int run_area_lights_command(std::string_view area_resref, std::string_view module_path, std::string_view user_path)
{
    if (area_resref.empty()) {
        print_usage();
        return 1;
    }

    if (!init_kernel_services(module_path, user_path)) {
        return 1;
    }

    auto shutdown = [] { nw::kernel::services().shutdown(); };
    const nw::Resref area{area_resref};
    if (!nw::kernel::resman().contains({area, nw::ResourceType::are})) {
        LOG_F(ERROR, "Area does not exist: {}", area_resref);
        shutdown();
        return 1;
    }

    nw::ObjectManager::AreaLoadProfile profile{};
    auto* loaded_area = nw::kernel::objects().make_area(area, &profile);
    if (!loaded_area) {
        LOG_F(ERROR, "Failed to load area: {}", area_resref);
        shutdown();
        return 1;
    }

    if (!loaded_area->instantiate() || !loaded_area->tileset) {
        LOG_F(ERROR, "Failed to instantiate area or tileset: {}", area_resref);
        nw::kernel::objects().destroy(loaded_area->handle());
        shutdown();
        return 1;
    }

    AreaLightReport report{};
    report.area = std::string(area_resref);
    report.tileset = loaded_area->tileset_resref.string();
    report.width = loaded_area->width;
    report.height = loaded_area->height;
    report.interior = (loaded_area->flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    report.underground = (loaded_area->flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    report.day_night_cycle = loaded_area->weather.day_night_cycle != 0;
    report.night = loaded_area->weather.is_night != 0;
    report.tiles = loaded_area->tiles.size();
    report.tuning = area_light_tuning(*loaded_area);

    static constexpr float k_tile_size = 10.0f;
    static constexpr size_t k_sample_limit = 32;
    const float height_step = loaded_area->tileset->tile_height;

    for (int y = 0; y < loaded_area->height; ++y) {
        for (int x = 0; x < loaded_area->width; ++x) {
            const size_t index = static_cast<size_t>(y * loaded_area->width + x);
            if (index >= loaded_area->tiles.size()) {
                continue;
            }

            const auto& tile = loaded_area->tiles[index];
            if (tile.id < 0 || static_cast<size_t>(tile.id) >= loaded_area->tileset->tiles.size()) {
                continue;
            }

            const auto& tile_def = loaded_area->tileset->tiles[static_cast<size_t>(tile.id)];
            if (tile_def.model.empty()) {
                continue;
            }

            ++report.model_counts[tile_def.model.c_str()];

            const TileLightSlots slots = area_tile_light_slots(tile);
            if (has_tile_light_slots(slots)) {
                ++report.tiles_with_light_slots;
                ++report.slot_counts[{
                    slots.main1,
                    slots.main2,
                    slots.source1,
                    slots.source2,
                }];
            }

            auto* mdl = nw::kernel::models().load(tile_def.model);
            if (!mdl || !mdl->valid()) {
                ++report.missing_models;
                continue;
            }

            const float world_x = static_cast<float>(x) * k_tile_size + 5.0f;
            const float world_y = static_cast<float>(y) * k_tile_size + 5.0f;
            const float world_z = static_cast<float>(tile.height) * height_step;
            const float angle = glm::radians(90.0f * static_cast<float>(tile.orientation));
            glm::mat4 placement = glm::translate(glm::mat4{1.0f}, glm::vec3{world_x, world_y, world_z});
            placement *= glm::toMat4(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));

            for (const auto& node : mdl->model.nodes) {
                const auto* light = dynamic_cast<const nw::model::LightNode*>(node.get());
                if (!light) {
                    continue;
                }

                const glm::mat4 model_transform = model_node_transform(*light);
                glm::vec3 position = glm::vec3(placement * model_transform[3]);
                const bool uses_main_slot = model_light_is_main_tile_slot(*light);
                if (has_tile_light_slots(slots)) {
                    const float z_offset = uses_main_slot ? 1.6f : 1.1f;
                    position.z = std::min(position.z, world_z + z_offset);
                }
                const glm::vec3 color = area_model_light_color(*light, slots);
                const float authored_radius = light_authored_radius(*light);
                const float radius = area_model_light_radius(*light, slots, report.tuning);
                const float authored_multiplier = scalar_controller(*light, nw::model::ControllerType::Multiplier)
                    .value_or(light->multiplier);
                const float intensity = area_model_light_intensity(*light, slots, report.tuning);
                const float color_max = color_max_channel(color);
                const bool first_light = report.tile_model_lights == 0;

                ++report.tile_model_lights;
                if (uses_main_slot) {
                    ++report.main_like_lights;
                } else {
                    ++report.source_like_lights;
                }
                if (color_max > 1.0e-4f) {
                    ++report.colored_lights;
                }

                report.max_color = std::max(report.max_color, color_max);
                if (first_light) {
                    report.min_radius = report.max_radius = radius;
                    report.min_intensity = report.max_intensity = intensity;
                    report.min_z = report.max_z = position.z;
                } else {
                    report.min_radius = std::min(report.min_radius, radius);
                    report.max_radius = std::max(report.max_radius, radius);
                    report.min_intensity = std::min(report.min_intensity, intensity);
                    report.max_intensity = std::max(report.max_intensity, intensity);
                    report.min_z = std::min(report.min_z, position.z);
                    report.max_z = std::max(report.max_z, position.z);
                }

                if (report.samples.size() < k_sample_limit) {
                    report.samples.push_back(AreaLightSample{
                        .tile_x = x,
                        .tile_y = y,
                        .tile_id = tile.id,
                        .tile_height = tile.height,
                        .tile_orientation = tile.orientation,
                        .model = tile_def.model.c_str(),
                        .node = light->name.c_str(),
                        .slots = slots,
                        .slot_kind = uses_main_slot ? "main" : "source",
                        .position = position,
                        .color = color,
                        .authored_radius = authored_radius,
                        .radius = radius,
                        .authored_multiplier = authored_multiplier,
                        .intensity = intensity,
                    });
                }
            }
        }
    }

    std::cout << "placeables:\n";
    for (size_t i = 0; i < loaded_area->placeables.size(); ++i) {
        const auto* placeable = loaded_area->placeables[i];
        if (!placeable || placeable->appearance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            continue;
        }

        const auto* info = nw::kernel::rules().placeables.get(
            nw::PlaceableType::make(static_cast<int32_t>(placeable->appearance)));
        std::cout << "  [" << i << "] appearance=" << placeable->appearance
                  << " tag=" << (placeable->common.tag ? placeable->common.tag.view() : "")
                  << " resref=" << placeable->common.resref.string()
                  << " model=" << (info ? info->model.string() : "?")
                  << " has_light=" << (info && info->has_light() ? 1 : 0)
                  << " bp_light_color=" << (info ? info->light_color : -2)
                  << " inst_light_color=" << placeable->light_color
                  << '\n';
        if (!info || !info->has_light()) {
            continue;
        }

        const glm::vec3 color = placeable_table_light_color(*info);
        if (!has_visible_light_color(color)) {
            continue;
        }

        const glm::vec3 offset{info->light_offset_x, info->light_offset_y, info->light_offset_z};
        const glm::vec3 position = glm::vec3(
            area_object_placement_transform(placeable->common.location) * glm::vec4{offset, 1.0f});

        ++report.placeable_table_lights;
        ++report.placeable_light_color_counts[info->light_color];
        const float color_max = color_max_channel(color);
        report.placeable_max_color = std::max(report.placeable_max_color, color_max);
        if (color_max > 1.0e-4f) {
            ++report.placeable_colored_lights;
        }

        if (report.placeable_samples.size() < k_sample_limit) {
            report.placeable_samples.push_back(AreaPlaceableLightSample{
                .index = i,
                .appearance = placeable->appearance,
                .tag = placeable->common.tag ? std::string{placeable->common.tag.view()} : std::string{},
                .resref = placeable->common.resref.string(),
                .label = info->label,
                .model = info->model.string(),
                .light_color = info->light_color,
                .offset = offset,
                .position = position,
                .color = color,
            });
        }
    }

    report.unique_slot_sets = report.slot_counts.size();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "area-lights: " << report.area << '\n';
    std::cout << "tileset: " << report.tileset
              << " size=" << report.width << "x" << report.height
              << " tiles=" << report.tiles
              << " flags interior=" << (report.interior ? 1 : 0)
              << " underground=" << (report.underground ? 1 : 0)
              << " cycle=" << (report.day_night_cycle ? 1 : 0)
              << " night=" << (report.night ? 1 : 0) << '\n';
    std::cout << "area-colors: sun_ambient=0x" << std::hex << loaded_area->weather.color_sun_ambient
              << " sun_diffuse=0x" << loaded_area->weather.color_sun_diffuse
              << " moon_ambient=0x" << loaded_area->weather.color_moon_ambient
              << " moon_diffuse=0x" << loaded_area->weather.color_moon_diffuse
              << std::dec << '\n';
    std::cout << "load-profile: demand=" << profile.demand_ms
              << "ms deserialize=" << profile.deserialize_ms
              << "ms creatures=" << profile.creatures
              << " doors=" << profile.doors
              << " placeables=" << profile.placeables
              << " triggers=" << profile.triggers
              << " encounters=" << profile.encounters << '\n';
    std::cout << "tile-slots: tiles_with_slots=" << report.tiles_with_light_slots
              << " unique_slot_sets=" << report.unique_slot_sets
              << " model_missing=" << report.missing_models << '\n';
    std::cout << "lights: total=" << report.tile_model_lights
              << " colored=" << report.colored_lights
              << " main_like=" << report.main_like_lights
              << " source_like=" << report.source_like_lights
              << " color_max=" << report.max_color
              << " radius=[" << report.min_radius << ".." << report.max_radius << "]"
              << " intensity=[" << report.min_intensity << ".." << report.max_intensity << "]"
              << " z=[" << report.min_z << ".." << report.max_z << "]"
              << " scale[r=" << report.tuning.radius_scale
              << ", i=" << report.tuning.intensity_scale << "]\n";

    std::cout << "placeable-lights: total=" << report.placeable_table_lights
              << " colored=" << report.placeable_colored_lights
              << " color_max=" << report.placeable_max_color << '\n';

    std::cout << "placeable light colors:\n";
    if (report.placeable_light_color_counts.empty()) {
        std::cout << "  <none>\n";
    } else {
        for (const auto& [color_id, count] : report.placeable_light_color_counts) {
            std::cout << "  " << color_id << ": " << count << '\n';
        }
    }

    std::cout << "slot sets:\n";
    if (report.slot_counts.empty()) {
        std::cout << "  <none>\n";
    } else {
        size_t printed = 0;
        for (const auto& [slots, count] : report.slot_counts) {
            if (printed >= 20) {
                std::cout << "  ... " << (report.slot_counts.size() - printed) << " more\n";
                break;
            }
            std::cout << "  " << static_cast<int>(slots[0]) << ','
                      << static_cast<int>(slots[1]) << ','
                      << static_cast<int>(slots[2]) << ','
                      << static_cast<int>(slots[3]) << ": " << count << '\n';
            ++printed;
        }
    }

    std::cout << "top tile models:\n";
    if (report.model_counts.empty()) {
        std::cout << "  <none>\n";
    } else {
        std::vector<std::pair<std::string, size_t>> models{
            report.model_counts.begin(),
            report.model_counts.end(),
        };
        std::sort(models.begin(), models.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first < b.first;
        });
        for (size_t i = 0; i < std::min<size_t>(models.size(), 12); ++i) {
            std::cout << "  " << models[i].first << ": " << models[i].second << '\n';
        }
    }

    std::cout << "samples:\n";
    if (report.samples.empty()) {
        std::cout << "  <none>\n";
    } else {
        for (const auto& sample : report.samples) {
            std::cout << "  tile=(" << sample.tile_x << ',' << sample.tile_y << ")"
                      << " id=" << sample.tile_id
                      << " h=" << sample.tile_height
                      << " orient=" << sample.tile_orientation
                      << " model=" << sample.model
                      << " node=" << sample.node
                      << " slot=" << sample.slot_kind
                      << " slots=(" << light_slots_string(sample.slots) << ")"
                      << " pos=(";
            print_vec3(sample.position);
            std::cout << ") color=(";
            print_vec3(sample.color);
            std::cout << ") raw_radius=" << sample.authored_radius
                      << " radius=" << sample.radius
                      << " raw_mult=" << sample.authored_multiplier
                      << " intensity=" << sample.intensity << '\n';
        }
    }

    std::cout << "placeable samples:\n";
    if (report.placeable_samples.empty()) {
        std::cout << "  <none>\n";
    } else {
        for (const auto& sample : report.placeable_samples) {
            std::cout << "  index=" << sample.index
                      << " appearance=" << sample.appearance
                      << " tag=" << sample.tag
                      << " resref=" << sample.resref
                      << " label=" << sample.label
                      << " model=" << sample.model
                      << " color_id=" << sample.light_color
                      << " offset=(";
            print_vec3(sample.offset);
            std::cout << ") pos=(";
            print_vec3(sample.position);
            std::cout << ") color=(";
            print_vec3(sample.color);
            std::cout << ")\n";
        }
    }

    nw::kernel::objects().destroy(loaded_area->handle());
    shutdown();
    return 0;
}

int run_texture_command(std::string_view resref, const std::filesystem::path& output_path,
    std::string_view module_path, std::string_view user_path)
{
    if (resref.empty() || output_path.empty()) {
        print_usage();
        return 1;
    }

    if (!init_kernel_services(module_path, user_path)) {
        return 1;
    }

    auto shutdown = [] { nw::kernel::services().shutdown(); };
    auto resolved = nw::render::viewer::preferred_plt_bitmap(resref, resref);
    if (resolved == resref && resref.size() >= 4 && resref[0] == 'p'
        && (resref[1] == 'm' || resref[1] == 'f') && resref[2] != 'h') {
        resolved = std::string(resref);
        resolved[2] = 'h';
    }
    std::optional<nw::Image> plt_image;
    auto* img = nw::kernel::resman().texture(nw::Resref(resolved));
    if (!img || !img->valid()) {
        auto plt_data = nw::kernel::resman().demand({nw::Resref(resolved), nw::ResourceType::plt});
        nw::Plt plt{std::move(plt_data)};
        if (!plt.valid()) {
            LOG_F(ERROR, "Failed to load texture: {}", resref);
            shutdown();
            return 1;
        }

        plt_image.emplace(plt, nw::PltColors{});
        if (!plt_image->valid()) {
            LOG_F(ERROR, "Failed to decode plt texture: {}", resolved);
            shutdown();
            return 1;
        }
        img = &*plt_image;
    }

    const bool ok = img->write_to(output_path);
    if (!ok) {
        LOG_F(ERROR, "Failed to write texture: {} -> {}", resref, output_path.string());
        shutdown();
        return 1;
    }

    LOG_F(INFO, "Saved texture: {} -> {} ({}x{} channels={})",
        resolved, output_path.string(), img->width(), img->height(), img->channels());
    shutdown();
    return 0;
}

std::optional<std::string> resolve_vfx_source(std::string_view query, std::string_view stage)
{
    if (query.empty()) {
        return std::nullopt;
    }

    const auto parsed_stage = resolve_vfx_stage(stage);
    if (!parsed_stage) {
        LOG_F(ERROR,
            "Unknown VFX stage '{}'. Expected one of: proj, cast, conj, impact, duration, cessation",
            stage);
        return std::nullopt;
    }

    if (std::filesystem::exists(query)) {
        return std::string(query);
    }

    if (resource_exists(query)) {
        return std::string(query);
    }

    if (auto visualeffect_resolved = resolve_visualeffect_vfx_source(query, *parsed_stage)) {
        return visualeffect_resolved;
    }

    if (auto spell_resolved = resolve_spell_vfx_source(query, *parsed_stage)) {
        return spell_resolved;
    }

    LOG_F(ERROR,
        "Unable to resolve VFX '{}'. Try a model resref like 'vpr_fireball', a spell label like 'fireball', or a visualeffect label like 'VFX_DUR_BLUR'. Use --stage proj|cast|conj|impact|duration|cessation",
        query);
    return std::nullopt;
}

std::optional<VfxSequence> resolve_spell_sequence(std::string_view spell_id)
{
    const auto* spells = nw::kernel::twodas().get("spells");
    if (!spells) {
        return std::nullopt;
    }

    const auto row = parse_2da_row_index(spell_id, *spells);
    if (!row) {
        LOG_F(ERROR, "Spell '{}' is not a valid spells.2da row id", spell_id);
        return std::nullopt;
    }

    return build_spell_sequence(*spells, *row);
}

std::optional<VfxSequence> resolve_vfx_sequence(std::string_view query)
{
    if (query.empty()) {
        return std::nullopt;
    }

    if (std::filesystem::exists(query) || resource_exists(query)) {
        return std::nullopt;
    }

    if (auto visualeffect = resolve_vfx_sequence_from_visualeffect(query)) {
        return visualeffect;
    }
    return resolve_vfx_sequence_from_spell(query);
}

} // namespace mudl
