#include "particle_tools.hpp"

#include "viewer_runtime.hpp"

#include <nw/render/viewer/preview_plt.hpp>

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/log.hpp>
#include <nw/model/mdl_particle_import.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_json.hpp>
#include <nw/render/particle_renderer.hpp>
#include <nw/render/particle_system.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mudl {
namespace {

using json = nlohmann::json;

json vec3_json(const glm::vec3& value)
{
    return json::array({value.x, value.y, value.z});
}

json quat_json(const glm::quat& value)
{
    return json::array({value.x, value.y, value.z, value.w});
}

json mat4_json(const glm::mat4& value)
{
    json rows = json::array();
    for (int row = 0; row < 4; ++row) {
        rows.push_back(json::array({
            value[0][row],
            value[1][row],
            value[2][row],
            value[3][row],
        }));
    }
    return rows;
}

glm::vec3 basis_scale(const glm::mat4& value)
{
    return {
        glm::length(glm::vec3{value[0]}),
        glm::length(glm::vec3{value[1]}),
        glm::length(glm::vec3{value[2]}),
    };
}

struct ParticlePreviewBounds {
    glm::vec2 min{std::numeric_limits<float>::max()};
    glm::vec2 max{std::numeric_limits<float>::lowest()};
};

struct ParticlePreviewAsset {
    struct TextureImage {
        std::unique_ptr<nw::Image> image;
        std::optional<nw::Image> plt_image;
    };

    std::unique_ptr<nw::model::Mdl> owned_mdl;
    const nw::model::Mdl* mdl = nullptr;
    std::string animation_name;
    float animation_length = 0.0f;
    std::vector<TextureImage> material_images;
    nw::model::ParticleImportResult import;
    nw::render::ParticleCompileResult compiled;
};

ParticlePreviewBounds pad_particle_preview_bounds(const ParticlePreviewBounds& bounds);

glm::vec2 project_particle_preview_point(const glm::vec3& p, ParticlePreviewView view)
{
    switch (view) {
    case ParticlePreviewView::front:
        return {p.x, p.z};
    case ParticlePreviewView::top:
        return {p.x, p.y};
    case ParticlePreviewView::side:
        return {p.y, p.z};
    }
    return {p.x, p.z};
}

std::string_view particle_preview_view_name(ParticlePreviewView view)
{
    switch (view) {
    case ParticlePreviewView::front:
        return "front";
    case ParticlePreviewView::top:
        return "top";
    case ParticlePreviewView::side:
        return "side";
    }
    return "front";
}

std::string_view particle_kernel_name(nw::render::CompiledParticleKernel kernel)
{
    using Kernel = nw::render::CompiledParticleKernel;
    switch (kernel) {
    case Kernel::sprite_basic_constant:
        return "sprite_basic_constant";
    case Kernel::sprite_basic:
        return "sprite_basic";
    case Kernel::sprite_target_gravity:
        return "sprite_target_gravity";
    case Kernel::sprite_target_bezier:
        return "sprite_target_bezier";
    case Kernel::linked_chain:
        return "linked_chain";
    case Kernel::beam_lightning:
        return "beam_lightning";
    case Kernel::mesh_basic:
        return "mesh_basic";
    }
    return "sprite_basic_constant";
}

std::string_view particle_render_mode_name(nw::render::ParticleRenderMode mode)
{
    using Mode = nw::render::ParticleRenderMode;
    switch (mode) {
    case Mode::billboard:
        return "billboard";
    case Mode::billboard_local_z:
        return "billboard_local_z";
    case Mode::billboard_world_z:
        return "billboard_world_z";
    case Mode::aligned_world_z:
        return "aligned_world_z";
    case Mode::velocity_aligned:
        return "velocity_aligned";
    case Mode::stretched:
        return "stretched";
    case Mode::linked_chain:
        return "linked_chain";
    case Mode::beam:
        return "beam";
    case Mode::mesh:
        return "mesh";
    }
    return "billboard";
}

std::string_view particle_targeting_mode_name(nw::render::ParticleTargetingMode mode)
{
    using Mode = nw::render::ParticleTargetingMode;
    switch (mode) {
    case Mode::none:
        return "none";
    case Mode::point_gravity:
        return "point_gravity";
    case Mode::point_bezier:
        return "point_bezier";
    case Mode::beam_lightning:
        return "beam_lightning";
    }
    return "none";
}

std::string_view particle_simulation_space_name(nw::render::ParticleSimulationSpace space)
{
    using Space = nw::render::ParticleSimulationSpace;
    switch (space) {
    case Space::world:
        return "world";
    case Space::local:
        return "local";
    case Space::emitter_attached:
        return "emitter_attached";
    case Space::spawn_attached:
        return "spawn_attached";
    }
    return "world";
}

std::string_view particle_emission_mode_name(nw::render::ParticleEmissionMode mode)
{
    using Mode = nw::render::ParticleEmissionMode;
    switch (mode) {
    case Mode::continuous:
        return "continuous";
    case Mode::single_shot:
        return "single_shot";
    case Mode::event_burst:
        return "event_burst";
    case Mode::beam_continuous:
        return "beam_continuous";
    }
    return "continuous";
}

std::string_view particle_spawn_metric_name(nw::render::ParticleSpawnMetric metric)
{
    using Metric = nw::render::ParticleSpawnMetric;
    switch (metric) {
    case Metric::per_second:
        return "per_second";
    case Metric::per_distance:
        return "per_distance";
    }
    return "per_second";
}

float eval_particle_curve_keys(const std::vector<nw::render::ParticleCurveKeyF32>& keys, float time, float fallback)
{
    if (keys.empty()) { return fallback; }
    if (time <= keys.front().time) { return keys.front().value; }
    if (time >= keys.back().time) { return keys.back().value; }
    for (size_t i = 1; i < keys.size(); ++i) {
        if (time > keys[i].time) { continue; }
        const auto& a = keys[i - 1];
        const auto& b = keys[i];
        const float denom = b.time - a.time;
        if (std::abs(denom) <= 1.0e-6f) { return b.value; }
        const float t = std::clamp((time - a.time) / denom, 0.0f, 1.0f);
        return a.value + (b.value - a.value) * t;
    }
    return keys.back().value;
}

bool write_text_file(const std::filesystem::path& path, std::string_view text)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path);
    if (!out) {
        LOG_F(ERROR, "Failed to open text output: {}", path.string());
        return false;
    }
    out << text;
    if (!out) {
        LOG_F(ERROR, "Failed to write text output: {}", path.string());
        return false;
    }
    return true;
}

std::filesystem::path particle_preview_metadata_path(const std::filesystem::path& image_path)
{
    auto out = image_path;
    out.replace_extension(".json");
    return out;
}

json make_particle_preview_summary_json(const ParticlePreviewAsset& asset, std::string_view model_spec,
    ParticlePreviewView view)
{
    json result = {
        {"model_spec", model_spec},
        {"effect_name", asset.import.effect.name},
        {"animation", asset.animation_name},
        {"view", particle_preview_view_name(view)},
        {"emitters", json::array()},
        {"import_warnings", json::array()},
        {"compile_warnings", json::array()},
    };

    for (size_t i = 0; i < asset.compiled.effect.emitters.size(); ++i) {
        const auto& emitter = asset.compiled.effect.emitters[i];
        const auto material_id = emitter.material;
        const auto* material = material_id < asset.import.effect.materials.size()
            ? &asset.import.effect.materials[material_id]
            : nullptr;
        const auto* init = i < asset.import.emitter_inits.size() ? &asset.import.emitter_inits[i] : nullptr;
        result["emitters"].push_back({
            {"index", i},
            {"name", asset.import.effect.emitters[i].name},
            {"kernel", particle_kernel_name(emitter.kernel)},
            {"render_mode", particle_render_mode_name(emitter.render.mode)},
            {"targeting_mode", particle_targeting_mode_name(emitter.targeting.mode)},
            {"simulation_space", particle_simulation_space_name(emitter.simulation_space)},
            {"emission_mode", particle_emission_mode_name(emitter.emission.mode)},
            {"spawn_metric", particle_spawn_metric_name(emitter.emission.metric)},
            {"max_particles", emitter.max_particles},
            {"material", {
                             {"index", material_id},
                             {"texture", material ? material->texture : ""},
                             {"mesh", material ? material->mesh : ""},
                         }},
            {"import_init", init ? json{
                                       {"emitter_node_name", init->emitter_node_name},
                                       {"has_default_transform", init->has_default_transform},
                                       {"default_transform", mat4_json(init->default_transform)},
                                       {"default_position", vec3_json(init->default_position)},
                                       {"default_orientation", quat_json(init->default_orientation)},
                                       {"default_scale", vec3_json(basis_scale(init->default_transform))},
                                       {"target_node_name", init->target_node_name},
                                       {"has_default_target_offset", init->has_default_target_offset},
                                       {"default_target_offset", vec3_json(init->default_target_offset)},
                                   }
                                 : json::object()},
        });
    }

    for (const auto& warning : asset.import.warnings) {
        result["import_warnings"].push_back({
            {"emitter", warning.emitter},
            {"field", warning.field},
            {"message", warning.message},
        });
    }
    for (const auto& warning : asset.compiled.warnings) {
        result["compile_warnings"].push_back({
            {"emitter", warning.emitter},
            {"message", warning.message},
        });
    }

    return result;
}

json make_particle_preview_frame_json(const ParticlePreviewAsset& asset,
    const nw::render::ParticleSystemInstance& system, int frame_index, float time_seconds)
{
    std::vector<uint32_t> live_by_emitter(system.emitters.size(), 0);
    std::vector<glm::vec3> min_by_emitter(system.emitters.size(), glm::vec3{std::numeric_limits<float>::max()});
    std::vector<glm::vec3> max_by_emitter(system.emitters.size(), glm::vec3{std::numeric_limits<float>::lowest()});
    std::vector<std::array<glm::vec3, 3>> sample_positions(system.emitters.size());
    std::vector<uint32_t> sample_counts(system.emitters.size(), 0);
    for (auto emitter_id : system.particles.core.emitter_id) {
        if (emitter_id < live_by_emitter.size()) {
            ++live_by_emitter[emitter_id];
        }
    }
    for (size_t i = 0; i < system.particles.core.position.size(); ++i) {
        const auto emitter_id = system.particles.core.emitter_id[i];
        if (emitter_id >= live_by_emitter.size()) {
            continue;
        }
        const auto& position = system.particles.core.position[i];
        min_by_emitter[emitter_id] = glm::min(min_by_emitter[emitter_id], position);
        max_by_emitter[emitter_id] = glm::max(max_by_emitter[emitter_id], position);
        if (sample_counts[emitter_id] < sample_positions[emitter_id].size()) {
            sample_positions[emitter_id][sample_counts[emitter_id]++] = position;
        }
    }

    json result = {
        {"frame", frame_index},
        {"time_seconds", time_seconds},
        {"live_particles", system.particles.core.position.size()},
        {"beam_rows", system.particles.beams.source_position.size()},
        {"emitters", json::array()},
    };

    for (size_t i = 0; i < system.emitters.size(); ++i) {
        const auto& state = system.emitters[i];
        const auto& emitter = asset.compiled.effect.emitters[i];
        const glm::vec3 position = glm::vec3(state.world_transform[3]);
        const float current_rate = eval_particle_curve_keys(
            emitter.emission_rate_track.keys, state.time, emitter.emission.rate);
        json emitter_json = {
            {"index", i},
            {"name", asset.import.effect.emitters[i].name},
            {"kernel", particle_kernel_name(emitter.kernel)},
            {"active", state.active},
            {"emitter_time", state.time},
            {"current_rate", current_rate},
            {"spawn_accumulator", state.spawn_accumulator},
            {"distance_accumulator", state.distance_accumulator},
            {"live_particles", live_by_emitter[i]},
            {"world_transform", mat4_json(state.world_transform)},
            {"world_scale", vec3_json(basis_scale(state.world_transform))},
            {"world_position", {position.x, position.y, position.z}},
            {"target_point", {state.target_point.x, state.target_point.y, state.target_point.z}},
        };
        if (live_by_emitter[i] > 0) {
            emitter_json["live_position_min"] = vec3_json(min_by_emitter[i]);
            emitter_json["live_position_max"] = vec3_json(max_by_emitter[i]);
            emitter_json["live_position_extent"] = vec3_json(max_by_emitter[i] - min_by_emitter[i]);
            json samples = json::array();
            for (uint32_t sample = 0; sample < sample_counts[i]; ++sample) {
                samples.push_back(vec3_json(sample_positions[i][sample]));
            }
            emitter_json["sample_positions"] = std::move(samples);
        }
        result["emitters"].push_back(std::move(emitter_json));
    }

    return result;
}

json make_scene_particle_frame_json(const nw::render::viewer::SceneParticleSystem& scene_particles, int frame_index, float time_seconds)
{
    std::vector<uint32_t> live_by_emitter(scene_particles.system.emitters.size(), 0);
    std::vector<glm::vec3> min_by_emitter(scene_particles.system.emitters.size(), glm::vec3{std::numeric_limits<float>::max()});
    std::vector<glm::vec3> max_by_emitter(scene_particles.system.emitters.size(), glm::vec3{std::numeric_limits<float>::lowest()});
    std::vector<std::array<glm::vec3, 3>> sample_positions(scene_particles.system.emitters.size());
    std::vector<json> sample_particles(scene_particles.system.emitters.size(), json::array());
    std::vector<uint32_t> sample_counts(scene_particles.system.emitters.size(), 0);

    for (size_t i = 0; i < scene_particles.system.particles.core.position.size(); ++i) {
        const auto emitter_id = scene_particles.system.particles.core.emitter_id[i];
        if (emitter_id >= live_by_emitter.size()) {
            continue;
        }
        ++live_by_emitter[emitter_id];
    }

    for (size_t i = 0; i < scene_particles.system.particles.core.position.size(); ++i) {
        const auto emitter_id = scene_particles.system.particles.core.emitter_id[i];
        if (emitter_id >= live_by_emitter.size()) {
            continue;
        }
        const auto& position = scene_particles.system.particles.core.position[i];
        min_by_emitter[emitter_id] = glm::min(min_by_emitter[emitter_id], position);
        max_by_emitter[emitter_id] = glm::max(max_by_emitter[emitter_id], position);
        if (sample_counts[emitter_id] < sample_positions[emitter_id].size()) {
            sample_positions[emitter_id][sample_counts[emitter_id]++] = position;
        }
        const bool include_particle = live_by_emitter[emitter_id] <= 128u || sample_particles[emitter_id].size() < 8u;
        if (!include_particle) {
            continue;
        }
        const uint32_t rgba8 = scene_particles.system.particles.core.color_rgba8[i];
        sample_particles[emitter_id].push_back({
            {"position", vec3_json(position)},
            {"age", scene_particles.system.particles.core.age[i]},
            {"lifetime", scene_particles.system.particles.core.lifetime[i]},
            {"normalized_age", scene_particles.system.particles.core.normalized_age[i]},
            {"size_x", scene_particles.system.particles.core.size_x[i]},
            {"size_y", scene_particles.system.particles.core.size_y[i]},
            {"rotation", scene_particles.system.particles.core.rotation[i]},
            {"rotation_rate", scene_particles.system.particles.core.rotation_rate[i]},
            {"frame", scene_particles.system.particles.core.frame[i]},
            {"frame_offset", scene_particles.system.particles.core.frame_offset[i]},
            {"velocity", vec3_json(scene_particles.system.particles.core.velocity[i])},
            {"render_direction", i < scene_particles.system.particles.core.render_direction.size() ? vec3_json(scene_particles.system.particles.core.render_direction[i]) : json::array()},
            {"color_rgba8", rgba8},
            {"alpha", static_cast<float>((rgba8 >> 24u) & 0xffu) / 255.0f},
        });
    }

    json result = {
        {"owner_model", scene_particles.owner && scene_particles.owner->mdl_ ? scene_particles.owner->mdl_->model.name : ""},
        {"owner_render_enabled", scene_particles.owner ? scene_particles.owner->render_enabled : false},
        {"owner_root_transform", scene_particles.owner ? mat4_json(scene_particles.owner->root_transform()) : json::array()},
        {"owner_animation", scene_particles.owner && scene_particles.owner->anim_ ? scene_particles.owner->anim_->name : ""},
        {"owner_animation_time_seconds", scene_particles.animation_time},
        {"frame", frame_index},
        {"time_seconds", time_seconds},
        {"live_particles", scene_particles.system.particles.core.position.size()},
        {"beam_rows", scene_particles.system.particles.beams.source_position.size()},
        {"emitters", json::array()},
    };

    for (size_t i = 0; i < scene_particles.system.emitters.size(); ++i) {
        const auto& state = scene_particles.system.emitters[i];
        const auto& emitter = scene_particles.compiled.effect.emitters[i];
        const auto* init = i < scene_particles.import.emitter_inits.size() ? &scene_particles.import.emitter_inits[i] : nullptr;
        const glm::vec3 position = glm::vec3(state.world_transform[3]);
        const float current_rate = eval_particle_curve_keys(
            emitter.emission_rate_track.keys, state.time, emitter.emission.rate);
        json emitter_json = {
            {"index", i},
            {"name", scene_particles.import.effect.emitters[i].name},
            {"kernel", particle_kernel_name(emitter.kernel)},
            {"render_mode", particle_render_mode_name(emitter.render.mode)},
            {"targeting_mode", particle_targeting_mode_name(emitter.targeting.mode)},
            {"simulation_space", particle_simulation_space_name(emitter.simulation_space)},
            {"spawn_metric", particle_spawn_metric_name(emitter.emission.metric)},
            {"active", state.active},
            {"emitter_time", state.time},
            {"current_rate", current_rate},
            {"spawn_accumulator", state.spawn_accumulator},
            {"distance_accumulator", state.distance_accumulator},
            {"frame_delta", vec3_json(state.frame_delta)},
            {"linear_velocity", vec3_json(state.linear_velocity)},
            {"live_particles", live_by_emitter[i]},
            {"world_transform", mat4_json(state.world_transform)},
            {"world_scale", vec3_json(basis_scale(state.world_transform))},
            {"world_position", vec3_json(position)},
            {"prev_world_pos", vec3_json(state.prev_world_pos)},
            {"target_point", vec3_json(state.target_point)},
            {"import_init", init ? json{
                                       {"emitter_node_name", init->emitter_node_name},
                                       {"has_default_transform", init->has_default_transform},
                                       {"default_transform", mat4_json(init->default_transform)},
                                       {"default_position", vec3_json(init->default_position)},
                                       {"default_orientation", quat_json(init->default_orientation)},
                                       {"target_node_name", init->target_node_name},
                                       {"has_default_target_offset", init->has_default_target_offset},
                                       {"default_target_offset", vec3_json(init->default_target_offset)},
                                   }
                                 : json::object()},
        };
        if (live_by_emitter[i] > 0) {
            emitter_json["live_position_min"] = vec3_json(min_by_emitter[i]);
            emitter_json["live_position_max"] = vec3_json(max_by_emitter[i]);
            emitter_json["live_position_extent"] = vec3_json(max_by_emitter[i] - min_by_emitter[i]);
            json samples = json::array();
            for (uint32_t sample = 0; sample < sample_counts[i]; ++sample) {
                samples.push_back(vec3_json(sample_positions[i][sample]));
            }
            emitter_json["sample_positions"] = std::move(samples);
            emitter_json["sample_particles"] = std::move(sample_particles[i]);
        }
        result["emitters"].push_back(std::move(emitter_json));
    }

    return result;
}

json dump_2da_row(const nw::StaticTwoDA& table, size_t row, std::initializer_list<std::string_view> columns)
{
    json result = {
        {"row", row},
        {"columns", json::object()},
    };
    for (auto column : columns) {
        nw::String value;
        if (table.get_to(row, column, value, false)) {
            result["columns"][std::string(column)] = value;
        }
    }
    return result;
}

void reset_live_vfx_sequence(AppState& state)
{
    vfx_sequence_reset_runtime(state);
}

void update_live_vfx_sequence(AppState& state, int32_t dt_ms)
{
    vfx_sequence_update_runtime(state, dt_ms);
}

std::unique_ptr<nw::render::viewer::PreviewScene> build_live_spell_scene(AppState& state, const VfxSequence& sequence)
{
    vfx_sequence_clear_state(state);

    auto scene = std::make_unique<nw::render::viewer::PreviewScene>();
    if (sequence.use_spell_actors) {
        if (auto caster = nw::render::nwn::load_model(kVfxSequenceCasterModel)) {
            scene->add(std::move(caster));
        }
        if (auto target = nw::render::nwn::load_model(kVfxSequenceTargetModel)) {
            scene->add(std::move(target));
        }
    }
    for (const auto& step : sequence.steps) {
        if (auto model = nw::render::nwn::load_model(step.source)) {
            scene->add(std::move(model));
        }
    }
    if (scene->models.empty()) {
        return {};
    }

    vfx_sequence_prepare_scene(state, *scene, sequence);
    return scene;
}

void expand_particle_preview_bounds(ParticlePreviewBounds& bounds, const glm::vec2& point)
{
    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

bool particle_preview_bounds_valid(const ParticlePreviewBounds& bounds)
{
    return bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y;
}

ParticlePreviewBounds pad_particle_preview_bounds(const ParticlePreviewBounds& bounds)
{
    ParticlePreviewBounds result = bounds;
    glm::vec2 size = result.max - result.min;
    if (size.x < 0.5f) {
        result.min.x -= 0.25f;
        result.max.x += 0.25f;
    }
    if (size.y < 0.5f) {
        result.min.y -= 0.25f;
        result.max.y += 0.25f;
    }
    return result;
}

void expand_particle_preview_bounds_for_region(ParticlePreviewBounds& bounds, const glm::vec3& center,
    const nw::render::ParticleSpawnRegion& region, ParticlePreviewView view)
{
    if (region.type != nw::render::ParticleSpawnRegionType::rect) {
        expand_particle_preview_bounds(bounds, project_particle_preview_point(center, view));
        return;
    }

    const glm::vec3 half_extents{
        0.5f * region.size.x * 0.01f,
        0.5f * region.size.y * 0.01f,
        0.0f,
    };

    const std::array<glm::vec3, 4> corners{{
        center + glm::vec3{-half_extents.x, -half_extents.y, 0.0f},
        center + glm::vec3{half_extents.x, -half_extents.y, 0.0f},
        center + glm::vec3{half_extents.x, half_extents.y, 0.0f},
        center + glm::vec3{-half_extents.x, half_extents.y, 0.0f},
    }};
    for (const auto& corner : corners) {
        expand_particle_preview_bounds(bounds, project_particle_preview_point(corner, view));
    }
}

void particle_preview_blend(std::vector<uint8_t>& rgba, int width, int height, int x, int y, const glm::vec4& color)
{
    if (x < 0 || y < 0 || x >= width || y >= height) { return; }
    const size_t idx = static_cast<size_t>((y * width + x) * 4);
    const float src_a = std::clamp(color.a, 0.0f, 1.0f);
    const float dst_a = rgba[idx + 3] / 255.0f;
    const float out_a = src_a + dst_a * (1.0f - src_a);
    auto blend_chan = [&](int chan, float src) {
        const float dst = rgba[idx + chan] / 255.0f;
        const float out = out_a > 1.0e-6f ? (src * src_a + dst * dst_a * (1.0f - src_a)) / out_a : 0.0f;
        rgba[idx + chan] = static_cast<uint8_t>(std::clamp(out, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    blend_chan(0, color.r);
    blend_chan(1, color.g);
    blend_chan(2, color.b);
    rgba[idx + 3] = static_cast<uint8_t>(std::clamp(out_a, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void particle_preview_add(std::vector<uint8_t>& rgba, int width, int height, int x, int y, const glm::vec4& color,
    float rgb_scale)
{
    if (x < 0 || y < 0 || x >= width || y >= height) { return; }
    const size_t idx = static_cast<size_t>((y * width + x) * 4);
    const float src_a = std::clamp(color.a, 0.0f, 1.0f);
    auto add_chan = [&](int chan, float src) {
        const float dst = rgba[idx + chan] / 255.0f;
        const float out = std::clamp(dst + src * rgb_scale, 0.0f, 1.0f);
        rgba[idx + chan] = static_cast<uint8_t>(out * 255.0f + 0.5f);
    };
    add_chan(0, color.r);
    add_chan(1, color.g);
    add_chan(2, color.b);
    const float dst_a = rgba[idx + 3] / 255.0f;
    rgba[idx + 3] = static_cast<uint8_t>(std::clamp(std::max(dst_a, src_a), 0.0f, 1.0f) * 255.0f + 0.5f);
}

void particle_preview_clear(std::vector<uint8_t>& rgba, int width, int height, const glm::vec4& color)
{
    rgba.resize(static_cast<size_t>(width * height * 4));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>((y * width + x) * 4);
            rgba[idx + 0] = static_cast<uint8_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[idx + 1] = static_cast<uint8_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[idx + 2] = static_cast<uint8_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f);
            rgba[idx + 3] = static_cast<uint8_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

glm::vec2 particle_preview_to_screen(const glm::vec2& point, const ParticlePreviewBounds& bounds, int width, int height)
{
    glm::vec2 size = bounds.max - bounds.min;
    if (size.x < 1.0e-4f) { size.x = 1.0f; }
    if (size.y < 1.0e-4f) { size.y = 1.0f; }
    const float scale = 0.88f * std::min(static_cast<float>(width) / size.x, static_cast<float>(height) / size.y);
    const glm::vec2 center = 0.5f * (bounds.min + bounds.max);
    const glm::vec2 delta = (point - center) * scale;
    return {
        static_cast<float>(width) * 0.5f + delta.x,
        static_cast<float>(height) * 0.5f - delta.y,
    };
}

void particle_preview_draw_circle(std::vector<uint8_t>& rgba, int width, int height, const glm::vec2& center,
    float radius, const glm::vec4& color)
{
    const int min_x = static_cast<int>(std::floor(center.x - radius));
    const int max_x = static_cast<int>(std::ceil(center.x + radius));
    const int min_y = static_cast<int>(std::floor(center.y - radius));
    const int max_y = static_cast<int>(std::ceil(center.y + radius));
    const float r2 = radius * radius;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = static_cast<float>(x) - center.x;
            const float dy = static_cast<float>(y) - center.y;
            if (dx * dx + dy * dy <= r2) {
                particle_preview_blend(rgba, width, height, x, y, color);
            }
        }
    }
}

void particle_preview_draw_soft_sprite(std::vector<uint8_t>& rgba, int width, int height, const glm::vec2& center,
    float radius_x, float radius_y, const glm::vec4& color)
{
    const int min_x = static_cast<int>(std::floor(center.x - radius_x));
    const int max_x = static_cast<int>(std::ceil(center.x + radius_x));
    const int min_y = static_cast<int>(std::floor(center.y - radius_y));
    const int max_y = static_cast<int>(std::ceil(center.y + radius_y));
    const float inv_rx = radius_x > 1.0e-6f ? 1.0f / radius_x : 1.0f;
    const float inv_ry = radius_y > 1.0e-6f ? 1.0f / radius_y : 1.0f;

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float dx = (static_cast<float>(x) - center.x) * inv_rx;
            const float dy = (static_cast<float>(y) - center.y) * inv_ry;
            const float d2 = dx * dx + dy * dy;
            if (d2 > 1.0f) { continue; }

            const float falloff = std::pow(1.0f - d2, 1.6f);
            particle_preview_blend(rgba, width, height, x, y,
                {color.r, color.g, color.b, color.a * falloff});
        }
    }
}

glm::vec4 particle_preview_sample_image(const nw::Image& image, float u, float v, const glm::vec4& tint)
{
    if (!image.valid() || image.width() == 0 || image.height() == 0 || !image.data()) {
        return tint;
    }

    const uint32_t channels = std::max<uint32_t>(1, image.channels());
    const uint32_t x = std::min<uint32_t>(
        static_cast<uint32_t>(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width() - 1)),
        image.width() - 1);
    const uint32_t y = std::min<uint32_t>(
        static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height() - 1)),
        image.height() - 1);
    const size_t idx = static_cast<size_t>((y * image.width() + x) * channels);
    const auto* pixels = image.data();
    const float src_r = pixels[idx + 0] / 255.0f;
    const float src_g = channels > 1 ? pixels[idx + 1] / 255.0f : src_r;
    const float src_b = channels > 2 ? pixels[idx + 2] / 255.0f : src_r;
    const float src_a = channels > 3 ? pixels[idx + 3] / 255.0f : 1.0f;
    return {
        std::clamp(src_r * tint.r, 0.0f, 1.0f),
        std::clamp(src_g * tint.g, 0.0f, 1.0f),
        std::clamp(src_b * tint.b, 0.0f, 1.0f),
        std::clamp(src_a * tint.a, 0.0f, 1.0f),
    };
}

glm::vec4 particle_preview_sample_image_additive(const nw::Image& image, float u, float v, const glm::vec4& tint,
    bool preserve_texture_hue)
{
    if (!image.valid() || image.width() == 0 || image.height() == 0 || !image.data()) {
        return tint;
    }

    const uint32_t channels = std::max<uint32_t>(1, image.channels());
    const uint32_t x = std::min<uint32_t>(
        static_cast<uint32_t>(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width() - 1)),
        image.width() - 1);
    const uint32_t y = std::min<uint32_t>(
        static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height() - 1)),
        image.height() - 1);
    const size_t idx = static_cast<size_t>((y * image.width() + x) * channels);
    const auto* pixels = image.data();
    const float src_r = pixels[idx + 0] / 255.0f;
    const float src_g = channels > 1 ? pixels[idx + 1] / 255.0f : src_r;
    const float src_b = channels > 2 ? pixels[idx + 2] / 255.0f : src_r;
    const float src_a = channels > 3 ? pixels[idx + 3] / 255.0f : 1.0f;

    const float tint_chroma = std::max(std::abs(tint.r - tint.g),
        std::max(std::abs(tint.g - tint.b), std::abs(tint.b - tint.r)));
    const float texel_chroma = std::max(std::abs(src_r - src_g),
        std::max(std::abs(src_g - src_b), std::abs(src_b - src_r)));

    if (preserve_texture_hue && tint_chroma <= 0.02f && texel_chroma >= 0.08f) {
        return {
            std::clamp(src_r, 0.0f, 1.0f),
            std::clamp(src_g, 0.0f, 1.0f),
            std::clamp(src_b, 0.0f, 1.0f),
            std::clamp(src_a * tint.a, 0.0f, 1.0f),
        };
    }

    if (!preserve_texture_hue) {
        return {
            std::clamp(src_r * tint.r, 0.0f, 1.0f),
            std::clamp(src_g * tint.g, 0.0f, 1.0f),
            std::clamp(src_b * tint.b, 0.0f, 1.0f),
            std::clamp(src_a * tint.a, 0.0f, 1.0f),
        };
    }

    return {
        std::clamp(src_r * tint.r, 0.0f, 1.0f),
        std::clamp(src_g * tint.g, 0.0f, 1.0f),
        std::clamp(src_b * tint.b, 0.0f, 1.0f),
        std::clamp(src_a * tint.a, 0.0f, 1.0f),
    };
}

float particle_preview_sample_source_alpha(const nw::Image& image, float u, float v)
{
    if (!image.valid() || image.width() == 0 || image.height() == 0 || !image.data() || image.channels() < 4) {
        return 1.0f;
    }

    const uint32_t channels = image.channels();
    const uint32_t x = std::min<uint32_t>(
        static_cast<uint32_t>(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width() - 1)),
        image.width() - 1);
    const uint32_t y = std::min<uint32_t>(
        static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height() - 1)),
        image.height() - 1);
    const size_t idx = static_cast<size_t>((y * image.width() + x) * channels + 3);
    return image.data()[idx] / 255.0f;
}

void particle_preview_draw_sheet_sprite(std::vector<uint8_t>& rgba, int width, int height, const glm::vec2& center,
    float radius_x, float radius_y, const glm::vec4& color, const nw::render::ParticleSpriteSheet& sheet, uint16_t frame,
    const nw::Image* image = nullptr, nw::render::ParticleBlendMode blend = nw::render::ParticleBlendMode::alpha,
    bool gate_rgb_by_particle_alpha = true)
{
    const bool additive = blend == nw::render::ParticleBlendMode::additive;
    const uint16_t cols = std::max<uint16_t>(1, sheet.columns);
    const uint16_t rows = std::max<uint16_t>(1, sheet.rows);
    const uint16_t frame_begin = sheet.frame_begin;
    const uint16_t frame_end = std::max(frame_begin, sheet.frame_end);
    const uint16_t frame_count = static_cast<uint16_t>(frame_end - frame_begin + 1);
    if (!image || !image->valid()) {
        if (frame_count <= 1) {
            particle_preview_draw_soft_sprite(rgba, width, height, center, radius_x, radius_y, color);
            return;
        }
    } else if (image->channels() < 3) {
        particle_preview_draw_soft_sprite(rgba, width, height, center, radius_x, radius_y, color);
        return;
    }

    const auto frame_rect = nw::render::particle_sprite_sheet_frame_rect(sheet, frame, image);
    const uint16_t col = frame_rect.column;
    const uint16_t row = frame_rect.row;
    const float col_t = cols > 1 ? static_cast<float>(col) / static_cast<float>(cols - 1) : 0.5f;
    const float row_t = rows > 1 ? static_cast<float>(row) / static_cast<float>(rows - 1) : 0.5f;

    const float squash_x = 0.72f + 0.16f * static_cast<float>((col % 3u) + 1u);
    const float squash_y = 0.72f + 0.16f * static_cast<float>((row % 3u) + 1u);
    const float skew = (col_t - 0.5f) * 0.55f;
    const float notch = 0.12f + 0.08f * row_t;

    const int min_x = static_cast<int>(std::floor(center.x - radius_x));
    const int max_x = static_cast<int>(std::ceil(center.x + radius_x));
    const int min_y = static_cast<int>(std::floor(center.y - radius_y));
    const int max_y = static_cast<int>(std::ceil(center.y + radius_y));
    const float inv_rx = radius_x > 1.0e-6f ? 1.0f / radius_x : 1.0f;
    const float inv_ry = radius_y > 1.0e-6f ? 1.0f / radius_y : 1.0f;

    if (image && image->valid()) {
        const float frame_u0 = frame_rect.u0;
        const float frame_u1 = frame_rect.u1;
        const float frame_v0 = frame_rect.v0;
        const float frame_v1 = frame_rect.v1;

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const float dx = (static_cast<float>(x) - center.x) * inv_rx;
                const float dy = (static_cast<float>(y) - center.y) * inv_ry;
                const float d2 = dx * dx + dy * dy;
                if (d2 > 1.0f) { continue; }

                const float local_u = 0.5f * (dx + 1.0f);
                const float local_v = 0.5f * (dy + 1.0f);
                const float sample_u = glm::mix(frame_u0, frame_u1, std::clamp(local_u, 0.0f, 1.0f));
                const float sample_v = glm::mix(frame_v0, frame_v1, std::clamp(local_v, 0.0f, 1.0f));
                auto sample = additive
                    ? particle_preview_sample_image_additive(*image, sample_u, sample_v, color, gate_rgb_by_particle_alpha)
                    : particle_preview_sample_image(*image, sample_u, sample_v, color);
                const float falloff = std::pow(1.0f - d2, 1.35f);
                sample.a *= falloff;
                if (additive) {
                    const float rgb_scale = gate_rgb_by_particle_alpha
                        ? std::clamp(sample.a, 0.0f, 1.0f)
                        : std::clamp(particle_preview_sample_source_alpha(*image, sample_u, sample_v) * falloff, 0.0f, 1.0f);
                    particle_preview_add(rgba, width, height, x, y, sample, rgb_scale);
                } else {
                    particle_preview_blend(rgba, width, height, x, y, sample);
                }
            }
        }
        return;
    }

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            float dx = (static_cast<float>(x) - center.x) * inv_rx;
            float dy = (static_cast<float>(y) - center.y) * inv_ry;
            dx -= skew * dy;
            const float nx = dx / squash_x;
            const float ny = dy / squash_y;
            const float d2 = nx * nx + ny * ny;
            if (d2 > 1.0f) { continue; }

            const float band_x = std::abs(dx + 0.55f - col_t * 1.1f);
            const float band_y = std::abs(dy - 0.55f + row_t * 1.1f);
            const float notch_mask = (dx > 0.0f && dy > 0.0f && band_x < notch && band_y < notch) ? 0.35f : 1.0f;
            const float frame_phase = static_cast<float>(frame_rect.row * cols + frame_rect.column);
            const float stripe = 0.82f + 0.18f * std::sin((dx + dy + frame_phase) * 7.0f);
            const float falloff = std::pow(1.0f - d2, 1.35f) * notch_mask * stripe;
            const glm::vec4 sample{color.r, color.g, color.b, color.a * falloff};
            if (additive) {
                const float rgb_scale = gate_rgb_by_particle_alpha ? sample.a : falloff;
                particle_preview_add(rgba, width, height, x, y, sample, rgb_scale);
            } else {
                particle_preview_blend(rgba, width, height, x, y, sample);
            }
        }
    }
}

void particle_preview_draw_line(std::vector<uint8_t>& rgba, int width, int height, const glm::vec2& a, const glm::vec2& b,
    float thickness, const glm::vec4& color)
{
    const glm::vec2 delta = b - a;
    const float length = glm::length(delta);
    if (length <= 1.0e-5f) {
        particle_preview_draw_circle(rgba, width, height, a, thickness * 0.5f, color);
        return;
    }

    const glm::vec2 dir = delta / length;
    const glm::vec2 normal{-dir.y, dir.x};
    const float radius = thickness * 0.5f;
    const int min_x = static_cast<int>(std::floor(std::min(a.x, b.x) - radius - 1.0f));
    const int max_x = static_cast<int>(std::ceil(std::max(a.x, b.x) + radius + 1.0f));
    const int min_y = static_cast<int>(std::floor(std::min(a.y, b.y) - radius - 1.0f));
    const int max_y = static_cast<int>(std::ceil(std::max(a.y, b.y) + radius + 1.0f));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const glm::vec2 p{
                static_cast<float>(x) + 0.5f,
                static_cast<float>(y) + 0.5f,
            };
            const glm::vec2 ap = p - a;
            const float along = glm::dot(ap, dir);
            const float clamped_along = std::clamp(along, 0.0f, length);
            const glm::vec2 closest = a + dir * clamped_along;
            const float distance = glm::length(p - closest);
            if (distance <= radius) {
                const float alpha = radius > 1.0e-5f ? std::clamp(1.0f - distance / radius, 0.0f, 1.0f) : 1.0f;
                particle_preview_blend(rgba, width, height, x, y,
                    {color.r, color.g, color.b, color.a * alpha});
            }
            (void)normal;
        }
    }
}

void particle_preview_draw_arrow(std::vector<uint8_t>& rgba, int width, int height, const glm::vec2& a, const glm::vec2& b,
    float thickness, const glm::vec4& color)
{
    particle_preview_draw_line(rgba, width, height, a, b, thickness, color);
    const glm::vec2 delta = b - a;
    const float length = glm::length(delta);
    if (length <= 1.0e-4f) { return; }
    const glm::vec2 dir = delta / length;
    const glm::vec2 left{-dir.y, dir.x};
    const float head_len = std::max(6.0f, thickness * 4.0f);
    const glm::vec2 head_a = b - dir * head_len + left * (head_len * 0.45f);
    const glm::vec2 head_b = b - dir * head_len - left * (head_len * 0.45f);
    particle_preview_draw_line(rgba, width, height, b, head_a, thickness, color);
    particle_preview_draw_line(rgba, width, height, b, head_b, thickness, color);
}

void particle_preview_draw_rect(std::vector<uint8_t>& rgba, int width, int height,
    int x0, int y0, int x1, int y1, const glm::vec4& color)
{
    x0 = std::clamp(x0, 0, width);
    x1 = std::clamp(x1, 0, width);
    y0 = std::clamp(y0, 0, height);
    y1 = std::clamp(y1, 0, height);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            particle_preview_blend(rgba, width, height, x, y, color);
        }
    }
}

void particle_preview_draw_glyph(std::vector<uint8_t>& rgba, int width, int height, int x, int y,
    char c, int scale, const glm::vec4& color)
{
    static constexpr std::array<std::array<uint8_t, 5>, 37> font{{
        {{0, 0, 0, 0, 0}},
        {{14, 17, 19, 21, 25}},
        {{4, 12, 4, 4, 14}},
        {{14, 17, 2, 4, 31}},
        {{31, 2, 4, 17, 14}},
        {{2, 6, 10, 31, 2}},
        {{31, 16, 30, 1, 30}},
        {{14, 16, 30, 17, 14}},
        {{31, 1, 2, 4, 4}},
        {{14, 17, 14, 17, 14}},
        {{14, 17, 15, 1, 14}},
        {{14, 17, 31, 17, 17}},
        {{30, 17, 30, 17, 30}},
        {{14, 17, 16, 17, 14}},
        {{30, 17, 17, 17, 30}},
        {{31, 16, 30, 16, 31}},
        {{31, 16, 30, 16, 16}},
        {{14, 16, 23, 17, 14}},
        {{17, 17, 31, 17, 17}},
        {{14, 4, 4, 4, 14}},
        {{7, 2, 2, 18, 12}},
        {{17, 18, 28, 18, 17}},
        {{16, 16, 16, 16, 31}},
        {{17, 27, 21, 17, 17}},
        {{17, 25, 21, 19, 17}},
        {{14, 17, 17, 17, 14}},
        {{30, 17, 30, 16, 16}},
        {{14, 17, 17, 21, 10}},
        {{30, 17, 30, 18, 17}},
        {{15, 16, 14, 1, 30}},
        {{31, 4, 4, 4, 4}},
        {{17, 17, 17, 17, 14}},
        {{17, 17, 17, 10, 4}},
        {{17, 17, 21, 27, 17}},
        {{17, 10, 4, 10, 17}},
        {{17, 10, 4, 4, 4}},
        {{31, 2, 4, 8, 31}},
    }};

    auto glyph_index = [](char ch) -> size_t {
        if (ch == ' ') { return 0; }
        if (ch >= '0' && ch <= '9') { return static_cast<size_t>(1 + (ch - '0')); }
        if (ch >= 'A' && ch <= 'Z') { return static_cast<size_t>(11 + (ch - 'A')); }
        if (ch >= 'a' && ch <= 'z') { return static_cast<size_t>(11 + (ch - 'a')); }
        return 0;
    };

    const auto& glyph = font[glyph_index(c)];
    for (int row = 0; row < 5; ++row) {
        const uint8_t bits = glyph[static_cast<size_t>(row)];
        for (int col = 0; col < 5; ++col) {
            if (((bits >> (4 - col)) & 1u) == 0u) { continue; }
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    particle_preview_blend(rgba, width, height,
                        x + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
    }
}

void particle_preview_draw_text(std::vector<uint8_t>& rgba, int width, int height, int x, int y,
    const std::string& text, int scale, const glm::vec4& color)
{
    int pen_x = x;
    for (char c : text) {
        particle_preview_draw_glyph(rgba, width, height, pen_x, y, c, scale, color);
        pen_x += 6 * scale;
    }
}

std::vector<size_t> particle_preview_indices_for_emitter(const nw::render::ParticleSystemInstance& system, uint16_t emitter_id)
{
    std::vector<size_t> indices;
    const auto& core = system.particles.core;
    for (size_t i = 0; i < core.emitter_id.size(); ++i) {
        if (core.emitter_id[i] == emitter_id) {
            indices.push_back(i);
        }
    }
    return indices;
}

void particle_preview_draw_bezier_curve(std::vector<uint8_t>& rgba, int width, int height,
    const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
    const ParticlePreviewBounds& bounds, ParticlePreviewView view, float thickness, const glm::vec4& color)
{
    constexpr int segments = 32;
    glm::vec2 prev = particle_preview_to_screen(project_particle_preview_point(p0, view), bounds, width, height);
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float u = 1.0f - t;
        const glm::vec3 point = u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
        const glm::vec2 current = particle_preview_to_screen(project_particle_preview_point(point, view), bounds, width, height);
        particle_preview_draw_line(rgba, width, height, prev, current, thickness, color);
        prev = current;
    }
}

void particle_preview_draw_region(std::vector<uint8_t>& rgba, int width, int height,
    const nw::render::ParticleEmitterState& emitter, const nw::render::ParticleSpawnRegion& region,
    const ParticlePreviewBounds& bounds, ParticlePreviewView view, const glm::vec4& color)
{
    const glm::vec3 center = glm::vec3(emitter.world_transform[3]);
    if (region.type != nw::render::ParticleSpawnRegionType::rect) {
        const auto p = particle_preview_to_screen(project_particle_preview_point(center, view), bounds, width, height);
        particle_preview_draw_circle(rgba, width, height, p, 6.0f, color);
        return;
    }

    const glm::vec3 half_extents{
        0.5f * region.size.x * 0.01f,
        0.5f * region.size.y * 0.01f,
        0.0f,
    };
    const std::array<glm::vec3, 4> corners{{
        center + glm::vec3{-half_extents.x, -half_extents.y, 0.0f},
        center + glm::vec3{half_extents.x, -half_extents.y, 0.0f},
        center + glm::vec3{half_extents.x, half_extents.y, 0.0f},
        center + glm::vec3{-half_extents.x, half_extents.y, 0.0f},
    }};
    std::array<glm::vec2, 4> projected{};
    for (size_t i = 0; i < corners.size(); ++i) {
        projected[i] = particle_preview_to_screen(project_particle_preview_point(corners[i], view), bounds, width, height);
    }
    for (size_t i = 0; i < projected.size(); ++i) {
        particle_preview_draw_line(rgba, width, height, projected[i], projected[(i + 1) % projected.size()], 1.25f, color);
    }
}

glm::vec4 unpack_particle_color(uint32_t rgba8)
{
    const auto chan = [](uint32_t value) {
        return static_cast<float>(value & 0xffu) / 255.0f;
    };
    return {
        chan(rgba8 >> 0),
        chan(rgba8 >> 8),
        chan(rgba8 >> 16),
        chan(rgba8 >> 24),
    };
}

bool load_particle_preview_asset(const std::string& model_spec, std::string_view animation_name, ParticlePreviewAsset& out)
{
    if (std::filesystem::exists(model_spec)) {
        out.owned_mdl = std::make_unique<nw::model::Mdl>(std::filesystem::path{model_spec});
        if (!out.owned_mdl->valid()) {
            LOG_F(ERROR, "Failed to load mdl: {}", model_spec);
            return false;
        }
        out.mdl = out.owned_mdl.get();
    } else {
        out.mdl = nw::kernel::models().load(model_spec);
        if (!out.mdl) {
            LOG_F(ERROR, "Failed to load model resref: {}", model_spec);
            return false;
        }
    }

    if (!animation_name.empty() && !out.mdl->model.find_animation(animation_name)) {
        LOG_F(ERROR, "Failed to find animation '{}' on model '{}'", animation_name, model_spec);
        return false;
    }

    auto particle_animation = animation_name;
    if (particle_animation.empty()) {
        particle_animation = nw::render::viewer::preferred_model_animation_name(
            *out.mdl, nw::render::viewer::PreferredModelAnimationContext::particle_preview);
    }

    out.animation_name = std::string{particle_animation};
    out.import = nw::model::import_particle_effect(*out.mdl, particle_animation, false);
    out.compiled = nw::render::compile_particle_effect(out.import.effect);
    out.material_images.resize(out.import.effect.materials.size());
    if (!particle_animation.empty()) {
        if (const auto* anim = out.mdl->model.find_animation(particle_animation)) {
            out.animation_length = anim->length;
        }
    }
    return true;
}

const nw::Image* particle_preview_material_image(ParticlePreviewAsset& asset, uint32_t material_id)
{
    if (material_id >= asset.import.effect.materials.size() || material_id >= asset.material_images.size()) {
        return nullptr;
    }

    auto& cache = asset.material_images[material_id];
    if (cache.image) { return cache.image.get(); }
    if (cache.plt_image) { return &*cache.plt_image; }

    const auto& material = asset.import.effect.materials[material_id];
    if (material.texture.empty() || material.texture == "null") { return nullptr; }

    const std::string resolved = nw::render::viewer::preferred_plt_bitmap(asset.import.effect.name, material.texture);
    cache.image.reset(nw::kernel::resman().texture(nw::Resref(resolved)));
    if (cache.image && cache.image->valid()) {
        return cache.image.get();
    }
    cache.image.reset();

    auto plt_data = nw::kernel::resman().demand({nw::Resref(resolved), nw::ResourceType::plt});
    nw::Plt plt{std::move(plt_data)};
    if (!plt.valid()) {
        return nullptr;
    }

    cache.plt_image.emplace(plt, nw::PltColors{});
    if (!cache.plt_image->valid()) {
        cache.plt_image.reset();
        return nullptr;
    }
    return &*cache.plt_image;
}

nw::render::ParticleSystemInstance make_particle_preview_system(const ParticlePreviewAsset& asset)
{
    auto system = nw::render::create_particle_system(asset.compiled.effect);
    nw::model::apply_particle_import_inits(asset.import, system);
    return system;
}

void tick_particle_preview_system(const ParticlePreviewAsset& asset,
    nw::render::ParticleSystemInstance& system, float preview_time, float& animation_time, bool& include_start_time)
{
    const float dt = 1.0f / 30.0f;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(preview_time, dt) / dt)));
    for (int i = 0; i < steps; ++i) {
        if (asset.animation_length > 0.0f) {
            float current_time = std::fmod(animation_time + dt, asset.animation_length);
            if (current_time < 0.0f) {
                current_time += asset.animation_length;
            }
            nw::model::apply_particle_import_events(
                asset.import, system, animation_time, current_time, asset.animation_length, include_start_time);
            animation_time = current_time;
            include_start_time = false;
        }
        nw::render::tick_particle_system(system, dt);
    }
}

ParticlePreviewBounds particle_preview_compute_bounds(const nw::render::ParticleSystemInstance& system, ParticlePreviewView view)
{
    ParticlePreviewBounds bounds;
    const auto& core = system.particles.core;
    for (const auto& p : core.position) {
        expand_particle_preview_bounds(bounds, project_particle_preview_point(p, view));
    }
    for (size_t i = 0; i < system.particles.beams.source_position.size(); ++i) {
        expand_particle_preview_bounds(bounds, project_particle_preview_point(system.particles.beams.source_position[i], view));
        expand_particle_preview_bounds(bounds, project_particle_preview_point(system.particles.beams.target_position[i], view));
    }
    for (size_t i = 0; i < system.emitters.size(); ++i) {
        const auto& emitter = system.emitters[i];
        const auto& compiled = system.effect->emitters[i];
        const glm::vec3 center = glm::vec3(emitter.world_transform[3]);
        expand_particle_preview_bounds_for_region(bounds, center, compiled.region, view);
        expand_particle_preview_bounds(bounds, project_particle_preview_point(center, view));
        if (compiled.targeting.mode != nw::render::ParticleTargetingMode::none) {
            expand_particle_preview_bounds(bounds, project_particle_preview_point(emitter.target_point, view));
        }
    }

    if (!particle_preview_bounds_valid(bounds)) {
        bounds.min = {-1.0f, -1.0f};
        bounds.max = {1.0f, 1.0f};
    }

    return pad_particle_preview_bounds(bounds);
}

bool write_particle_preview_frame(ParticlePreviewAsset& asset,
    const nw::render::ParticleSystemInstance& system, const std::filesystem::path& out_path, ParticlePreviewView view)
{
    const auto bounds = particle_preview_compute_bounds(system, view);
    const auto& core = system.particles.core;
    const int width = 1024;
    const int height = 1024;
    std::vector<uint8_t> rgba;
    particle_preview_clear(rgba, width, height, {0.05f, 0.06f, 0.08f, 1.0f});

    const glm::vec2 origin = particle_preview_to_screen({0.0f, 0.0f}, bounds, width, height);
    particle_preview_draw_line(rgba, width, height, {0.0f, origin.y}, {static_cast<float>(width), origin.y}, 1.0f, {0.15f, 0.17f, 0.22f, 1.0f});
    particle_preview_draw_line(rgba, width, height, {origin.x, 0.0f}, {origin.x, static_cast<float>(height)}, 1.0f, {0.15f, 0.17f, 0.22f, 1.0f});

    for (size_t i = 0; i < system.particles.beams.source_position.size(); ++i) {
        const auto a = particle_preview_to_screen(project_particle_preview_point(system.particles.beams.source_position[i], view), bounds, width, height);
        const auto b = particle_preview_to_screen(project_particle_preview_point(system.particles.beams.target_position[i], view), bounds, width, height);
        particle_preview_draw_line(rgba, width, height, a, b, 2.5f, {0.45f, 0.85f, 1.0f, 0.9f});
        particle_preview_draw_circle(rgba, width, height, a, 4.0f, {1.0f, 0.8f, 0.3f, 1.0f});
        particle_preview_draw_circle(rgba, width, height, b, 4.0f, {1.0f, 0.4f, 0.4f, 1.0f});
    }

    for (size_t i = 0; i < system.emitters.size(); ++i) {
        const auto& emitter = system.emitters[i];
        const auto& compiled = system.effect->emitters[i];
        const glm::vec4 region_color = compiled.targeting.mode != nw::render::ParticleTargetingMode::none
            ? glm::vec4{0.25f, 0.9f, 0.95f, 0.45f}
            : glm::vec4{0.55f, 0.7f, 0.3f, 0.28f};
        particle_preview_draw_region(rgba, width, height, emitter, compiled.region, bounds, view, region_color);

        const auto emitter_screen = particle_preview_to_screen(
            project_particle_preview_point(glm::vec3(emitter.world_transform[3]), view), bounds, width, height);
        particle_preview_draw_circle(rgba, width, height, emitter_screen, 2.5f, {0.95f, 0.95f, 0.95f, 0.85f});

        if (compiled.targeting.mode != nw::render::ParticleTargetingMode::none) {
            const auto target = particle_preview_to_screen(
                project_particle_preview_point(emitter.target_point, view), bounds, width, height);
            particle_preview_draw_line(rgba, width, height, emitter_screen, target, 1.25f, {1.0f, 0.55f, 0.25f, 0.7f});
            particle_preview_draw_circle(rgba, width, height, target, 3.0f, {1.0f, 0.35f, 0.25f, 0.95f});
        }
    }

    for (size_t emitter_id = 0; emitter_id < system.emitters.size(); ++emitter_id) {
        const auto& emitter = system.emitters[emitter_id];
        const auto& compiled = system.effect->emitters[emitter_id];
        const auto indices = particle_preview_indices_for_emitter(system, static_cast<uint16_t>(emitter_id));

        if (compiled.targeting.mode == nw::render::ParticleTargetingMode::point_bezier) {
            glm::vec3 p0 = glm::vec3(emitter.world_transform[3]);
            glm::vec3 p3 = emitter.target_point;
            glm::vec3 delta = p3 - p0;
            const float length = glm::length(delta);
            glm::vec3 direction = length > 1.0e-6f ? delta / length : glm::vec3{0.0f, 0.0f, 1.0f};
            glm::vec3 p1 = p0 + direction * compiled.targeting.source_tangent;
            glm::vec3 p2 = p3 - direction * compiled.targeting.target_tangent;

            if (!indices.empty() && compiled.render.mode != nw::render::ParticleRenderMode::linked_chain) {
                const size_t idx = indices.front();
                p0 = system.particles.bezier.source_position[idx];
                p1 = system.particles.bezier.source_tangent[idx];
                p2 = system.particles.bezier.target_tangent[idx];
                p3 = system.particles.bezier.target_position[idx];
            }

            particle_preview_draw_bezier_curve(
                rgba, width, height, p0, p1, p2, p3, bounds, view, 1.0f, {0.95f, 0.55f, 0.2f, 0.45f});

            const auto c1 = particle_preview_to_screen(project_particle_preview_point(p1, view), bounds, width, height);
            const auto c2 = particle_preview_to_screen(project_particle_preview_point(p2, view), bounds, width, height);
            particle_preview_draw_circle(rgba, width, height, c1, 2.0f, {1.0f, 0.8f, 0.2f, 0.75f});
            particle_preview_draw_circle(rgba, width, height, c2, 2.0f, {1.0f, 0.65f, 0.2f, 0.75f});
        }

        if (compiled.render.mode == nw::render::ParticleRenderMode::linked_chain && indices.size() >= 2) {
            for (size_t j = 1; j < indices.size(); ++j) {
                const auto a = particle_preview_to_screen(
                    project_particle_preview_point(core.position[indices[j - 1]], view), bounds, width, height);
                const auto b = particle_preview_to_screen(
                    project_particle_preview_point(core.position[indices[j]], view), bounds, width, height);
                particle_preview_draw_line(rgba, width, height, a, b, 1.75f, {0.8f, 0.95f, 1.0f, 0.55f});
            }
        }

        if (compiled.targeting.mode == nw::render::ParticleTargetingMode::point_gravity) {
            const auto target = particle_preview_to_screen(
                project_particle_preview_point(emitter.target_point, view), bounds, width, height);
            for (size_t idx : indices) {
                const auto particle = particle_preview_to_screen(
                    project_particle_preview_point(core.position[idx], view), bounds, width, height);
                const glm::vec2 delta = target - particle;
                const float distance = glm::length(delta);
                if (distance < 3.0f) { continue; }
                const glm::vec2 end = particle + delta * std::min(0.35f, 24.0f / distance);
                particle_preview_draw_arrow(rgba, width, height, particle, end, 1.0f, {1.0f, 0.7f, 0.2f, 0.35f});
            }
        }
    }

    for (size_t i = 0; i < core.position.size(); ++i) {
        const auto emitter_id = core.emitter_id[i];
        const auto kernel = system.effect->emitters[emitter_id].kernel;
        if (kernel == nw::render::CompiledParticleKernel::beam_lightning) { continue; }
        const auto screen = particle_preview_to_screen(project_particle_preview_point(core.position[i], view), bounds, width, height);
        const float rx = std::clamp(6.0f + 14.0f * core.size_x[i], 4.0f, 56.0f);
        const float ry = std::clamp(6.0f + 14.0f * core.size_y[i], 4.0f, 56.0f);
        auto color = unpack_particle_color(core.color_rgba8[i]);
        color.a = std::clamp(color.a * 0.75f + 0.1f, 0.0f, 1.0f);
        const auto material_id = core.material_id[i];
        const auto blend = material_id < system.effect->materials.size()
            ? system.effect->materials[material_id].blend
            : nw::render::ParticleBlendMode::alpha;
        const auto render_mode = emitter_id < system.effect->emitters.size()
            ? system.effect->emitters[emitter_id].render.mode
            : nw::render::ParticleRenderMode::billboard;
        const bool gate_rgb_by_particle_alpha = render_mode != nw::render::ParticleRenderMode::billboard
            && render_mode != nw::render::ParticleRenderMode::billboard_local_z
            && render_mode != nw::render::ParticleRenderMode::billboard_world_z
            && render_mode != nw::render::ParticleRenderMode::aligned_world_z;
        const auto sheet = material_id < system.effect->materials.size()
            ? system.effect->materials[material_id].sheet
            : nw::render::ParticleSpriteSheet{};
        const auto* image = particle_preview_material_image(asset, material_id);
        particle_preview_draw_sheet_sprite(
            rgba, width, height, screen, rx, ry, color, sheet, core.frame[i], image, blend, gate_rgb_by_particle_alpha);

        if (system.effect->emitters[emitter_id].targeting.mode != nw::render::ParticleTargetingMode::none) {
            const glm::vec2 velocity_end = particle_preview_to_screen(
                project_particle_preview_point(core.position[i] + core.velocity[i] * 0.12f, view), bounds, width, height);
            if (glm::length(velocity_end - screen) >= 2.0f) {
                particle_preview_draw_arrow(rgba, width, height, screen, velocity_end, 1.0f, {0.9f, 0.95f, 0.55f, 0.3f});
            }
        }
    }

    std::vector<uint32_t> live_by_emitter(system.emitters.size(), 0);
    for (auto emitter_id : core.emitter_id) {
        if (emitter_id < live_by_emitter.size()) {
            ++live_by_emitter[emitter_id];
        }
    }

    const int panel_x = 14;
    const int panel_y = 14;
    const int panel_w = 540;
    const int line_h = 14;
    const int panel_h = 12 + static_cast<int>(system.emitters.size() + 1) * line_h;
    particle_preview_draw_rect(rgba, width, height,
        panel_x, panel_y, panel_x + panel_w, panel_y + panel_h, {0.02f, 0.03f, 0.05f, 0.78f});
    particle_preview_draw_text(rgba, width, height, panel_x + 8, panel_y + 6,
        "EMITTER KERNEL LIVE", 1, {0.85f, 0.9f, 1.0f, 0.95f});

    for (size_t i = 0; i < system.emitters.size(); ++i) {
        const auto& compiled = system.effect->emitters[i];
        std::string line = std::to_string(i);
        line += " ";
        line += asset.import.effect.emitters[i].name;
        line += " ";
        line += std::string(particle_kernel_name(compiled.kernel));
        line += " ";
        line += std::to_string(live_by_emitter[i]);

        glm::vec4 line_color{0.78f, 0.86f, 0.92f, 0.95f};
        if (compiled.targeting.mode == nw::render::ParticleTargetingMode::point_bezier) {
            line_color = {1.0f, 0.78f, 0.42f, 0.95f};
        } else if (compiled.targeting.mode == nw::render::ParticleTargetingMode::point_gravity) {
            line_color = {1.0f, 0.86f, 0.42f, 0.95f};
        } else if (compiled.render.mode == nw::render::ParticleRenderMode::linked_chain) {
            line_color = {0.75f, 0.95f, 1.0f, 0.95f};
        }

        particle_preview_draw_text(rgba, width, height, panel_x + 8,
            panel_y + 6 + static_cast<int>((i + 1) * line_h), line, 1, line_color);
    }

    if (!out_path.parent_path().empty()) {
        std::filesystem::create_directories(out_path.parent_path());
    }
    if (!stbi_write_png(out_path.string().c_str(), width, height, 4, rgba.data(), width * 4)) {
        LOG_F(ERROR, "Failed to write particle preview: {}", out_path.string());
        return false;
    }

    LOG_F(INFO, "Saved particle preview: {}", out_path.string());
    return true;
}

ParticlePreviewBounds spell_preview_compute_bounds(const nw::render::viewer::PreviewScene& scene, ParticlePreviewView view)
{
    ParticlePreviewBounds bounds;
    for (const auto& scene_particles : scene.particles) {
        const auto& system = scene_particles.system;
        const auto& core = system.particles.core;
        for (const auto& p : core.position) {
            expand_particle_preview_bounds(bounds, project_particle_preview_point(p, view));
        }
        for (size_t i = 0; i < system.particles.beams.source_position.size(); ++i) {
            expand_particle_preview_bounds(bounds, project_particle_preview_point(system.particles.beams.source_position[i], view));
            expand_particle_preview_bounds(bounds, project_particle_preview_point(system.particles.beams.target_position[i], view));
        }
        for (size_t i = 0; i < system.emitters.size(); ++i) {
            const auto& emitter = system.emitters[i];
            const auto& compiled = system.effect->emitters[i];
            expand_particle_preview_bounds(bounds, project_particle_preview_point(glm::vec3(emitter.world_transform[3]), view));
            expand_particle_preview_bounds_for_region(bounds, glm::vec3(emitter.world_transform[3]), compiled.region, view);
            if (compiled.targeting.mode != nw::render::ParticleTargetingMode::none) {
                expand_particle_preview_bounds(bounds, project_particle_preview_point(emitter.target_point, view));
            }
        }
    }
    if (!particle_preview_bounds_valid(bounds)) {
        bounds.min = {-1.0f, -1.0f};
        bounds.max = {1.0f, 1.0f};
    }
    return pad_particle_preview_bounds(bounds);
}

ParticlePreviewAsset* spell_preview_asset_for_particles(
    std::vector<ParticlePreviewAsset>& assets, const nw::render::viewer::SceneParticleSystem& scene_particles)
{
    const auto owner_name = scene_particles.owner && scene_particles.owner->mdl_
        ? std::string_view(scene_particles.owner->mdl_->model.name)
        : std::string_view{};
    for (auto& asset : assets) {
        if (asset.mdl && std::string_view(asset.mdl->model.name) == owner_name) {
            return &asset;
        }
    }
    return nullptr;
}

bool write_spell_preview_live_frame(std::vector<ParticlePreviewAsset>& assets, const nw::render::viewer::PreviewScene& scene,
    const std::filesystem::path& out_path, ParticlePreviewView view)
{
    const auto bounds = spell_preview_compute_bounds(scene, view);
    const int width = 1280;
    const int height = 720;
    std::vector<uint8_t> rgba;
    particle_preview_clear(rgba, width, height, {0.19f, 0.27f, 0.42f, 1.0f});

    for (const auto& scene_particles : scene.particles) {
        auto* asset = spell_preview_asset_for_particles(assets, scene_particles);
        if (!asset) {
            continue;
        }

        const auto& system = scene_particles.system;
        const auto& core = system.particles.core;
        for (size_t i = 0; i < core.position.size(); ++i) {
            const auto emitter_id = core.emitter_id[i];
            const auto kernel = system.effect->emitters[emitter_id].kernel;
            if (kernel == nw::render::CompiledParticleKernel::beam_lightning) { continue; }
            const auto screen = particle_preview_to_screen(
                project_particle_preview_point(core.position[i], view), bounds, width, height);
            const float rx = std::clamp(6.0f + 14.0f * core.size_x[i], 4.0f, 56.0f);
            const float ry = std::clamp(6.0f + 14.0f * core.size_y[i], 4.0f, 56.0f);
            auto color = unpack_particle_color(core.color_rgba8[i]);
            color.a = std::clamp(color.a * 0.75f + 0.1f, 0.0f, 1.0f);
            const auto material_id = core.material_id[i];
            const auto blend = material_id < system.effect->materials.size()
                ? system.effect->materials[material_id].blend
                : nw::render::ParticleBlendMode::alpha;
            const auto sheet = material_id < system.effect->materials.size()
                ? system.effect->materials[material_id].sheet
                : nw::render::ParticleSpriteSheet{};
            const auto* image = particle_preview_material_image(*asset, material_id);
            particle_preview_draw_sheet_sprite(rgba, width, height, screen, rx, ry, color, sheet, core.frame[i], image, blend);
        }
    }

    if (!out_path.parent_path().empty()) {
        std::filesystem::create_directories(out_path.parent_path());
    }
    if (!stbi_write_png(out_path.string().c_str(), width, height, 4, rgba.data(), width * 4)) {
        LOG_F(ERROR, "Failed to write spell preview: {}", out_path.string());
        return false;
    }

    LOG_F(INFO, "Saved spell preview: {}", out_path.string());
    return true;
}

} // namespace

int run_particle_preview_command(const std::string& model_spec, const std::filesystem::path& out_path,
    float preview_time, ParticlePreviewView view, std::string_view animation_name, const ParticlePreviewMetadataOptions& metadata)
{
    ParticlePreviewAsset asset;
    if (!load_particle_preview_asset(model_spec, animation_name, asset)) { return 1; }

    auto system = make_particle_preview_system(asset);
    float animation_time = 0.0f;
    bool include_start_time = true;
    tick_particle_preview_system(asset, system, preview_time, animation_time, include_start_time);
    if (!write_particle_preview_frame(asset, system, out_path, view)) { return 1; }
    if (metadata.write) {
        json payload = make_particle_preview_summary_json(asset, model_spec, view);
        payload["frame"] = make_particle_preview_frame_json(asset, system, 0, preview_time);
        if (!write_text_file(particle_preview_metadata_path(out_path), payload.dump(2))) {
            return 1;
        }
    }
    return 0;
}

int run_particle_preview_frames_command(const std::string& model_spec, const std::filesystem::path& out_dir,
    float duration, int fps, ParticlePreviewView view, std::string_view animation_name, const ParticlePreviewMetadataOptions& metadata)
{
    ParticlePreviewAsset asset;
    if (!load_particle_preview_asset(model_spec, animation_name, asset)) { return 1; }

    auto system = make_particle_preview_system(asset);
    const float dt = 1.0f / static_cast<float>(std::max(1, fps));
    const int frames = std::max(1, static_cast<int>(std::ceil(std::max(duration, dt) / dt)));
    float animation_time = 0.0f;
    bool include_start_time = true;
    std::filesystem::create_directories(out_dir);

    for (int i = 0; i < frames; ++i) {
        tick_particle_preview_system(asset, system, dt, animation_time, include_start_time);
        char file_name[32];
        std::snprintf(file_name, sizeof(file_name), "%04d.png", i);
        if (!write_particle_preview_frame(asset, system, out_dir / file_name, view)) { return 1; }
        if (metadata.write) {
            auto metadata_path = out_dir / file_name;
            metadata_path.replace_extension(".json");
            const float frame_time = (static_cast<float>(i) + 1.0f) * dt;
            if (!write_text_file(metadata_path,
                    make_particle_preview_frame_json(asset, system, i, frame_time).dump(2))) {
                return 1;
            }
        }
    }

    if (metadata.write) {
        if (!write_text_file(out_dir / "preview.json",
                make_particle_preview_summary_json(asset, model_spec, view).dump(2))) {
            return 1;
        }
    }

    return 0;
}

int run_particle_export_command(const std::string& model_spec, const std::filesystem::path& out_path,
    std::string_view animation_name)
{
    ParticlePreviewAsset asset;
    if (!load_particle_preview_asset(model_spec, animation_name, asset)) { return 1; }

    std::error_code ec;
    if (const auto parent = out_path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            LOG_F(ERROR, "Failed to create output directory '{}': {}", parent.string(), ec.message());
            return 1;
        }
    }

    std::ofstream output{out_path};
    if (!output.is_open()) {
        LOG_F(ERROR, "Failed to open output path '{}'", out_path.string());
        return 1;
    }

    const json payload = asset.import.effect;
    output << std::setw(2) << payload << '\n';
    if (!output.good()) {
        LOG_F(ERROR, "Failed to write particle JSON '{}'", out_path.string());
        return 1;
    }

    LOG_F(INFO, "Exported particle effect '{}' -> '{}'",
        asset.import.effect.name.empty() ? model_spec : asset.import.effect.name,
        out_path.string());
    for (const auto& warning : asset.import.warnings) {
        LOG_F(INFO, "Particle warning {}.{}: {}", warning.emitter, warning.field, warning.message);
    }

    return 0;
}

int run_spell_export_command(const VfxSequence& sequence, std::string_view spell_id,
    const std::filesystem::path& out_path)
{
    json payload = {
        {"spell_id", spell_id},
        {"label", sequence.label},
        {"source_target_layout", sequence.source_target_layout},
        {"use_spell_actors", sequence.use_spell_actors},
        {"source_target_distance", sequence.source_target_distance},
        {"steps", json::array()},
    };

    for (const auto& step : sequence.steps) {
        json step_json = {
            {"label", step.label},
            {"source", step.source},
            {"duration_ms", step.duration_ms},
            {"anchor", step.anchor},
            {"source_anchor", step.source_anchor},
            {"target_point_kind", std::string(vfx_target_point_kind_label(step.target_point_kind))},
            {"target_anchor", step.target_anchor},
            {"anchor_position_only", step.anchor_position_only},
            {"caster_animation", step.caster_animation},
            {"scale", step.scale},
        };

        ParticlePreviewAsset asset;
        if (load_particle_preview_asset(step.source, {}, asset)) {
            step_json["particle"] = make_particle_preview_summary_json(asset, step.source, ParticlePreviewView::front);
        }

        payload["steps"].push_back(std::move(step_json));
    }

    if (!write_text_file(out_path, payload.dump(2))) {
        return 1;
    }

    LOG_F(INFO, "Exported spell sequence '{}' -> '{}'", sequence.label, out_path.string());
    return 0;
}

int run_spell_export_live_command(AppState& state, const VfxSequence& sequence, std::string_view spell_id,
    const std::filesystem::path& out_path)
{
    auto scene = build_live_spell_scene(state, sequence);
    replace_current_scene_after_gpu_idle(state, std::move(scene));
    if (!state.current_scene) {
        LOG_F(ERROR, "Failed to load live spell sequence '{}'", sequence.label);
        return 1;
    }

    reset_live_vfx_sequence(state);
    update_live_vfx_sequence(state, 0);

    constexpr float dt = 1.0f / 30.0f;
    const int total_frames = std::max(
        1, static_cast<int>(std::ceil(static_cast<float>(std::max(state.vfx_sequence_loop_ms, 1)) / (dt * 1000.0f))));

    json payload = {
        {"spell_id", spell_id},
        {"label", sequence.label},
        {"source_target_layout", sequence.source_target_layout},
        {"use_spell_actors", sequence.use_spell_actors},
        {"source_target_distance", sequence.source_target_distance},
        {"loop_ms", state.vfx_sequence_loop_ms},
        {"steps", json::array()},
        {"frames", json::array()},
    };

    for (const auto& step : sequence.steps) {
        payload["steps"].push_back({
            {"label", step.label},
            {"source", step.source},
            {"duration_ms", step.duration_ms},
            {"anchor", step.anchor},
            {"source_anchor", step.source_anchor},
            {"target_point_kind", std::string(vfx_target_point_kind_label(step.target_point_kind))},
            {"target_anchor", step.target_anchor},
            {"anchor_position_only", step.anchor_position_only},
            {"caster_animation", step.caster_animation},
            {"scale", step.scale},
        });
    }

    if (const auto* spells = nw::kernel::twodas().get("spells")) {
        size_t row = 0;
        bool have_row = false;
        auto parsed = nw::string::from<size_t>(spell_id);
        if (parsed && *parsed < spells->rows()) {
            row = *parsed;
            have_row = true;
        }
        if (have_row) {
            payload["spells_row"] = dump_2da_row(*spells, row, {
                                                                   "Label",
                                                                   "ImpactScript",
                                                                   "ConjTime",
                                                                   "ConjAnim",
                                                                   "ConjHeadVisual",
                                                                   "ConjHandVisual",
                                                                   "ConjGrndVisual",
                                                                   "CastTime",
                                                                   "CastAnim",
                                                                   "CastHeadVisual",
                                                                   "CastHandVisual",
                                                                   "CastGrndVisual",
                                                                   "Proj",
                                                                   "ProjModel",
                                                                   "ProjType",
                                                                   "ProjSpwnPoint",
                                                                   "ProjOrientation",
                                                                   "HasProjectile",
                                                               });
        }
    }

    if (const auto* visualeffects = nw::kernel::twodas().get("visualeffects")) {
        if (visualeffects->rows() > 22) {
            nw::String label;
            if (visualeffects->get_to(22, "Label", label, false) && !label.empty()) {
                payload["classic_fireball_vfx"] = dump_2da_row(*visualeffects, 22, {
                                                                                       "Label",
                                                                                       "Type_FD",
                                                                                       "Imp_HeadCon_Node",
                                                                                       "Imp_Impact_Node",
                                                                                       "Imp_Root_M_Node",
                                                                                       "ProgFX_Impact",
                                                                                       "ProgFX_Duration",
                                                                                       "ProgFX_Cessation",
                                                                                       "Ces_HeadCon_Node",
                                                                                       "Ces_Impact_Node",
                                                                                   });
            }
        }
    }

    auto append_frame = [&](int frame_index, float elapsed_seconds) {
        json frame_json = {
            {"frame", frame_index},
            {"time_seconds", elapsed_seconds},
            {"sequence_time_ms", state.vfx_sequence_time_ms},
            {"active_step", state.vfx_sequence_active_step},
            {"models", json::array()},
            {"particles", json::array()},
        };

        for (size_t i = 0; i < state.current_scene->models.size(); ++i) {
            const auto& model = state.current_scene->models[i];
            frame_json["models"].push_back({
                {"index", i},
                {"name", model && model->mdl_ ? model->mdl_->model.name : ""},
                {"render_enabled", model ? model->render_enabled : false},
                {"animation", model && model->anim_ ? model->anim_->name : ""},
                {"animation_time_seconds", model ? static_cast<float>(model->anim_cursor_) * 0.001f : 0.0f},
                {"root_transform", model ? mat4_json(model->root_transform()) : json::array()},
            });
        }

        for (const auto& particles : state.current_scene->particles) {
            frame_json["particles"].push_back(make_scene_particle_frame_json(particles, frame_index, elapsed_seconds));
        }

        payload["frames"].push_back(std::move(frame_json));
    };

    append_frame(0, 0.0f);
    for (int frame = 1; frame <= total_frames; ++frame) {
        const int32_t dt_ms = static_cast<int32_t>(std::lround(dt * 1000.0f));
        update_live_vfx_sequence(state, dt_ms);
        state.current_scene->update(dt_ms);
        append_frame(frame, static_cast<float>(frame) * dt);
    }

    if (!write_text_file(out_path, payload.dump(2))) {
        return 1;
    }

    LOG_F(INFO, "Exported live spell sequence '{}' -> '{}'", sequence.label, out_path.string());
    return 0;
}

int run_spell_preview_live_command(AppState& state, const VfxSequence& sequence, std::string_view spell_id,
    const std::filesystem::path& out_path, int frame_index, ParticlePreviewView view,
    const ParticlePreviewMetadataOptions& metadata)
{
    std::vector<ParticlePreviewAsset> assets;
    assets.reserve(sequence.steps.size());
    for (const auto& step : sequence.steps) {
        ParticlePreviewAsset asset;
        if (!load_particle_preview_asset(step.source, {}, asset)) {
            LOG_F(ERROR, "Failed to load live spell preview asset '{}'", step.source);
            return 1;
        }
        assets.push_back(std::move(asset));
    }

    auto scene = build_live_spell_scene(state, sequence);
    replace_current_scene_after_gpu_idle(state, std::move(scene));
    if (!state.current_scene) {
        LOG_F(ERROR, "Failed to load live spell preview '{}'", sequence.label);
        return 1;
    }

    reset_live_vfx_sequence(state);
    update_live_vfx_sequence(state, 0);

    constexpr float dt = 1.0f / 30.0f;
    for (int frame = 0; frame < frame_index; ++frame) {
        const int32_t dt_ms = static_cast<int32_t>(std::lround(dt * 1000.0f));
        update_live_vfx_sequence(state, dt_ms);
        state.current_scene->update(dt_ms);
    }

    if (!write_spell_preview_live_frame(assets, *state.current_scene, out_path, view)) {
        return 1;
    }

    if (metadata.write) {
        const float elapsed_seconds = static_cast<float>(frame_index) * dt;
        json payload = {
            {"spell_id", spell_id},
            {"label", sequence.label},
            {"frame", frame_index},
            {"time_seconds", elapsed_seconds},
            {"view", particle_preview_view_name(view)},
            {"particles", json::array()},
        };
        for (const auto& particles : state.current_scene->particles) {
            payload["particles"].push_back(make_scene_particle_frame_json(particles, frame_index, elapsed_seconds));
        }
        if (!write_text_file(particle_preview_metadata_path(out_path), payload.dump(2))) {
            return 1;
        }
    }

    return 0;
}

} // namespace mudl
