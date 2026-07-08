#include "particle_compile.hpp"

#include "../util/profile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace nw::render {

namespace {

constexpr size_t kMaxCompiledEmitterCount = static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u;
constexpr size_t kMaxCompiledMaterialCount = static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u;
constexpr uint32_t kGlobalCompileWarningEmitter = std::numeric_limits<uint32_t>::max();

float sanitized_particle_float(float value, bool& sanitized) noexcept
{
    if (std::isfinite(value)) { return value; }
    sanitized = true;
    return 0.0f;
}

void sanitize_particle_vec2(glm::vec2& value, bool& sanitized) noexcept
{
    value.x = sanitized_particle_float(value.x, sanitized);
    value.y = sanitized_particle_float(value.y, sanitized);
}

void sanitize_particle_vec4(glm::vec4& value, bool& sanitized) noexcept
{
    value.x = sanitized_particle_float(value.x, sanitized);
    value.y = sanitized_particle_float(value.y, sanitized);
    value.z = sanitized_particle_float(value.z, sanitized);
    value.w = sanitized_particle_float(value.w, sanitized);
}

void sanitize_particle_range(ParticleRangeF32& value, bool& sanitized) noexcept
{
    value.min = sanitized_particle_float(value.min, sanitized);
    value.max = sanitized_particle_float(value.max, sanitized);
}

void sanitize_particle_curve(ParticleCurveF32& value, bool& sanitized) noexcept
{
    for (auto& key : value.keys) {
        key.time = sanitized_particle_float(key.time, sanitized);
        key.value = sanitized_particle_float(key.value, sanitized);
    }
}

void sanitize_particle_gradient(ParticleGradient& value, bool& sanitized) noexcept
{
    for (auto& key : value.keys) {
        key.time = sanitized_particle_float(key.time, sanitized);
        sanitize_particle_vec4(key.value, sanitized);
    }
}

uint32_t sprite_sheet_frame_count(const ParticleSpriteSheet& sheet) noexcept
{
    const uint32_t columns = std::max<uint32_t>(1u, sheet.columns);
    const uint32_t rows = std::max<uint32_t>(1u, sheet.rows);
    return columns * rows;
}

uint16_t sprite_sheet_max_frame_index(const ParticleSpriteSheet& sheet) noexcept
{
    const uint32_t max_index = sprite_sheet_frame_count(sheet) - 1u;
    return static_cast<uint16_t>(
        std::min<uint32_t>(max_index, std::numeric_limits<uint16_t>::max()));
}

ParticleMaterialDef sanitize_particle_material(ParticleMaterialDef material, bool& sanitized) noexcept
{
    if (material.sheet.columns == 0) {
        material.sheet.columns = 1;
    }
    if (material.sheet.rows == 0) {
        material.sheet.rows = 1;
    }
    material.sheet.frames_per_second = sanitized_particle_float(material.sheet.frames_per_second, sanitized);
    return material;
}

ParticleEmitterDef sanitize_particle_emitter(ParticleEmitterDef emitter, bool& sanitized) noexcept
{
    sanitize_particle_vec2(emitter.region.size, sanitized);

    sanitize_particle_range(emitter.initial.lifetime, sanitized);
    sanitize_particle_range(emitter.initial.speed, sanitized);
    sanitize_particle_range(emitter.initial.rotation, sanitized);
    sanitize_particle_range(emitter.initial.rotation_rate, sanitized);
    sanitize_particle_range(emitter.initial.size_x, sanitized);
    sanitize_particle_range(emitter.initial.size_y, sanitized);
    emitter.initial.spread_radians = sanitized_particle_float(emitter.initial.spread_radians, sanitized);
    emitter.initial.mass = sanitized_particle_float(emitter.initial.mass, sanitized);
    emitter.initial.velocity_inheritance = sanitized_particle_float(emitter.initial.velocity_inheritance, sanitized);
    sanitize_particle_vec4(emitter.initial.color, sanitized);

    sanitize_particle_curve(emitter.spawn_over_time.alpha_start, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.alpha_end, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.lifetime, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.speed, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.speed_random, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.mass, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.rotation_rate, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.spread, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.sheet_frame_begin, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.sheet_frame_end, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.sheet_fps, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.sheet_random_start, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.size_start_x, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.size_end_x, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.size_start_y, sanitized);
    sanitize_particle_curve(emitter.spawn_over_time.size_end_y, sanitized);
    sanitize_particle_gradient(emitter.spawn_over_time.color_start, sanitized);
    sanitize_particle_gradient(emitter.spawn_over_time.color_end, sanitized);

    sanitize_particle_curve(emitter.over_life.alpha, sanitized);
    sanitize_particle_curve(emitter.over_life.size_x, sanitized);
    sanitize_particle_curve(emitter.over_life.size_y, sanitized);
    sanitize_particle_curve(emitter.over_life.rotation, sanitized);
    sanitize_particle_gradient(emitter.over_life.color, sanitized);

    emitter.targeting.transition_factor = sanitized_particle_float(emitter.targeting.transition_factor, sanitized);
    emitter.targeting.gravity = sanitized_particle_float(emitter.targeting.gravity, sanitized);
    emitter.targeting.drag = sanitized_particle_float(emitter.targeting.drag, sanitized);
    emitter.targeting.kill_radius = sanitized_particle_float(emitter.targeting.kill_radius, sanitized);
    emitter.targeting.source_tangent = sanitized_particle_float(emitter.targeting.source_tangent, sanitized);
    emitter.targeting.target_tangent = sanitized_particle_float(emitter.targeting.target_tangent, sanitized);

    emitter.beam.update_interval = sanitized_particle_float(emitter.beam.update_interval, sanitized);
    emitter.beam.jitter_radius = sanitized_particle_float(emitter.beam.jitter_radius, sanitized);
    emitter.beam.noise_scale = sanitized_particle_float(emitter.beam.noise_scale, sanitized);

    emitter.force_event.radius = sanitized_particle_float(emitter.force_event.radius, sanitized);
    emitter.force_event.length = sanitized_particle_float(emitter.force_event.length, sanitized);
    emitter.collision.bounce_coefficient = sanitized_particle_float(emitter.collision.bounce_coefficient, sanitized);

    emitter.render.opacity_scale = sanitized_particle_float(emitter.render.opacity_scale, sanitized);
    emitter.render.blur_length = sanitized_particle_float(emitter.render.blur_length, sanitized);
    emitter.render.deadspace_radians = sanitized_particle_float(emitter.render.deadspace_radians, sanitized);

    emitter.emission.rate = sanitized_particle_float(emitter.emission.rate, sanitized);
    sanitize_particle_curve(emitter.emission.rate_over_time, sanitized);
    emitter.emission.effect_event_period = sanitized_particle_float(emitter.emission.effect_event_period, sanitized);
    return emitter;
}

template <typename Key>
bool keys_are_constant(const std::vector<Key>& keys)
{
    if (keys.size() <= 1) { return true; }
    const auto& first = keys.front().value;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (keys[i].value != first) { return false; }
    }
    return true;
}

template <typename Key>
std::vector<Key> sorted_keys(const std::vector<Key>& keys)
{
    std::vector<Key> result = keys;
    std::stable_sort(result.begin(), result.end(), [](const Key& lhs, const Key& rhs) {
        return lhs.time < rhs.time;
    });
    return result;
}

template <typename Key, typename Value>
Value keyed_end_value_or(const std::vector<Key>& keys, const Value& fallback)
{
    if (keys.empty()) {
        return fallback;
    }

    auto it = std::max_element(keys.begin(), keys.end(), [](const Key& lhs, const Key& rhs) {
        return lhs.time < rhs.time;
    });
    return it->value;
}

CompiledParticleScalarTrack compile_scalar_track(const ParticleCurveF32& curve, float default_value)
{
    if (curve.keys.empty()) {
        return {.keys = {{0.0f, default_value}}};
    }

    return {.keys = sorted_keys(curve.keys)};
}

CompiledParticleColorTrack compile_color_track(const ParticleGradient& gradient, const glm::vec4& default_value)
{
    if (gradient.keys.empty()) {
        return {.keys = {{0.0f, default_value}}};
    }

    return {.keys = sorted_keys(gradient.keys)};
}

uint32_t compile_features(const ParticleEmitterDef& emitter, const ParticleMaterialDef* material);

CompiledParticleKeyedScalarTrack compile_keyed_scalar_track(const ParticleCurveF32& curve, float fallback)
{
    if (curve.keys.empty()) {
        return {.keys = {{0.0f, fallback}}};
    }

    return {.keys = sorted_keys(curve.keys)};
}

CompiledParticleKeyedColorTrack compile_keyed_color_track(const ParticleGradient& gradient, const glm::vec4& fallback)
{
    if (gradient.keys.empty()) {
        return {.keys = {{0.0f, fallback}}};
    }

    return {.keys = sorted_keys(gradient.keys)};
}

CompiledParticleKernel classify_kernel(const ParticleEmitterDef& emitter, const ParticleMaterialDef* material)
{
    if (emitter.render.mode == ParticleRenderMode::beam
        || emitter.targeting.mode == ParticleTargetingMode::beam_lightning
        || emitter.emission.mode == ParticleEmissionMode::beam_continuous) {
        return CompiledParticleKernel::beam_lightning;
    }

    if (emitter.render.mode == ParticleRenderMode::linked_chain) {
        return CompiledParticleKernel::linked_chain;
    }

    switch (emitter.targeting.mode) {
    case ParticleTargetingMode::point_gravity:
        return CompiledParticleKernel::sprite_target_gravity;
    case ParticleTargetingMode::point_bezier:
        return CompiledParticleKernel::sprite_target_bezier;
    case ParticleTargetingMode::none:
    case ParticleTargetingMode::beam_lightning:
        break;
    }

    if (material && !material->mesh.empty()) { return CompiledParticleKernel::mesh_basic; }

    const uint32_t features = compile_features(emitter, material);
    if (features == 0) {
        return CompiledParticleKernel::sprite_basic_constant;
    }

    return CompiledParticleKernel::sprite_basic;
}

uint32_t compile_features(const ParticleEmitterDef& emitter, const ParticleMaterialDef* material)
{
    uint32_t features = 0;

    const bool has_spawn_size = !emitter.spawn_over_time.size_start_x.keys.empty()
        || !emitter.spawn_over_time.size_end_x.keys.empty()
        || !emitter.spawn_over_time.size_start_y.keys.empty()
        || !emitter.spawn_over_time.size_end_y.keys.empty();
    const bool has_over_life_size = !keys_are_constant(emitter.over_life.size_x.keys)
        || !keys_are_constant(emitter.over_life.size_y.keys);
    if (has_spawn_size) {
        features |= CompiledParticleFeature::sample_spawn_size;
    }
    if (has_spawn_size || has_over_life_size) {
        features |= CompiledParticleFeature::update_size;
    }
    if (!emitter.spawn_over_time.size_start_x.keys.empty()
        || !emitter.spawn_over_time.size_end_x.keys.empty()) {
        features |= CompiledParticleFeature::spawn_size_x_lerp;
    }
    if (!emitter.spawn_over_time.size_start_y.keys.empty()
        || !emitter.spawn_over_time.size_end_y.keys.empty()) {
        features |= CompiledParticleFeature::spawn_size_y_lerp;
    }

    const bool has_spawn_color = !emitter.spawn_over_time.color_start.keys.empty()
        || !emitter.spawn_over_time.color_end.keys.empty()
        || !emitter.spawn_over_time.alpha_start.keys.empty()
        || !emitter.spawn_over_time.alpha_end.keys.empty();
    const bool has_over_life_color = !keys_are_constant(emitter.over_life.color.keys)
        || !keys_are_constant(emitter.over_life.alpha.keys);
    if (has_spawn_color) {
        features |= CompiledParticleFeature::sample_spawn_color;
        features |= CompiledParticleFeature::spawn_color_lerp;
    }
    if (has_spawn_color || has_over_life_color) {
        features |= CompiledParticleFeature::update_color;
    }

    const bool has_over_life_rotation = !keys_are_constant(emitter.over_life.rotation.keys);
    const bool has_rotation_rate = emitter.initial.rotation_rate.min != 0.0f || emitter.initial.rotation_rate.max != 0.0f;
    if (has_over_life_rotation || has_rotation_rate) {
        features |= CompiledParticleFeature::update_rotation;
    }

    const bool has_spawn_motion = !emitter.spawn_over_time.lifetime.keys.empty()
        || !emitter.spawn_over_time.speed.keys.empty()
        || !emitter.spawn_over_time.speed_random.keys.empty()
        || !emitter.spawn_over_time.mass.keys.empty()
        || !emitter.spawn_over_time.rotation_rate.keys.empty()
        || !emitter.spawn_over_time.spread.keys.empty();
    if (has_spawn_motion) {
        features |= CompiledParticleFeature::sample_spawn_motion;
    }

    if (material) {
        const uint32_t max_frames = sprite_sheet_frame_count(material->sheet);
        const uint16_t frame_begin = material->sheet.frame_begin;
        uint16_t frame_end = std::max(frame_begin, material->sheet.frame_end);
        if (max_frames > 1 && frame_begin == 0 && frame_end == 0) {
            frame_end = sprite_sheet_max_frame_index(material->sheet);
        }
        const uint32_t frame_count = static_cast<uint32_t>(frame_end) - frame_begin + 1u;
        const bool has_sheet_keys = !emitter.spawn_over_time.sheet_frame_begin.keys.empty()
            || !emitter.spawn_over_time.sheet_frame_end.keys.empty()
            || !emitter.spawn_over_time.sheet_fps.keys.empty()
            || !emitter.spawn_over_time.sheet_random_start.keys.empty();
        if (has_sheet_keys) {
            features |= CompiledParticleFeature::sample_spawn_frame;
        }
        if (has_sheet_keys || frame_count > 1) {
            features |= CompiledParticleFeature::update_frame;
        }
    }

    return features;
}

uint32_t compile_storage(const ParticleEmitterDef& emitter)
{
    uint32_t storage = 0;

    if (emitter.simulation_space == ParticleSimulationSpace::local
        || emitter.simulation_space == ParticleSimulationSpace::emitter_attached
        || emitter.simulation_space == ParticleSimulationSpace::spawn_attached) {
        storage |= CompiledParticleStorage::attachment;
    }

    switch (emitter.targeting.mode) {
    case ParticleTargetingMode::point_bezier:
        storage |= CompiledParticleStorage::bezier;
        break;
    case ParticleTargetingMode::beam_lightning:
        storage |= CompiledParticleStorage::beams;
        break;
    case ParticleTargetingMode::none:
    case ParticleTargetingMode::point_gravity:
        break;
    }

    if (emitter.emission.mode == ParticleEmissionMode::beam_continuous
        || emitter.render.mode == ParticleRenderMode::beam) {
        storage |= CompiledParticleStorage::beams;
    }

    return storage;
}

} // namespace

ParticleCompileResult compile_particle_effect(const ParticleEffectDef& def)
{
    NW_PROFILE_SCOPE();
    NW_PROFILE_TEXT_CSTR(def.name.c_str());

    ParticleCompileResult result;
    const size_t material_count = std::min(def.materials.size(), kMaxCompiledMaterialCount);
    const size_t emitter_count = std::min(def.emitters.size(), kMaxCompiledEmitterCount);
    result.effect.materials.reserve(material_count);
    result.effect.emitters.reserve(emitter_count);
    std::vector<ParticleMaterialDef> sanitized_materials;
    sanitized_materials.reserve(material_count);

    if (def.materials.size() > material_count) {
        result.warnings.push_back(
            {kGlobalCompileWarningEmitter, "material count exceeds 16-bit runtime limit; truncating compiled material table"});
    }
    if (def.emitters.size() > emitter_count) {
        result.warnings.push_back(
            {kGlobalCompileWarningEmitter, "emitter count exceeds 16-bit runtime limit; truncating compiled emitter table"});
    }

    for (size_t i = 0; i < material_count; ++i) {
        bool sanitized_material_numeric = false;
        auto material = sanitize_particle_material(def.materials[i], sanitized_material_numeric);
        if (sanitized_material_numeric) {
            result.warnings.push_back(
                {kGlobalCompileWarningEmitter, "non-finite particle material numeric field sanitized to 0"});
        }
        auto& out = result.effect.materials.emplace_back();
        out.blend = material.blend;
        out.double_sided = material.double_sided;
        out.sheet = material.sheet;
        out.texture = 0;
        out.mesh = 0;
        sanitized_materials.push_back(std::move(material));
    }

    for (size_t i = 0; i < emitter_count; ++i) {
        NW_PROFILE_SCOPE_N("compile_particle_emitter");
        bool sanitized_emitter_numeric = false;
        auto emitter = sanitize_particle_emitter(def.emitters[i], sanitized_emitter_numeric);
        if (sanitized_emitter_numeric) {
            result.warnings.push_back(
                {static_cast<uint32_t>(i), "non-finite particle emitter numeric field sanitized to 0"});
        }
        auto& out = result.effect.emitters.emplace_back();
        NW_PROFILE_TEXT_CSTR(emitter.name.c_str());

        out.attachment = emitter.attachment;
        out.emission = emitter.emission;
        out.region = emitter.region;
        out.simulation_space = emitter.simulation_space;
        out.affected_by_wind = emitter.affected_by_wind;
        out.initial = emitter.initial;
        out.spawn_over_time = emitter.spawn_over_time;
        out.over_life = emitter.over_life;
        out.targeting = emitter.targeting;
        out.beam = emitter.beam;
        out.force_event = emitter.force_event;
        out.collision = emitter.collision;
        out.render = emitter.render;

        if (emitter.render.material >= result.effect.materials.size()) {
            out.material = 0;
            result.warnings.push_back(
                {static_cast<uint32_t>(i), "material index exceeds compiled range; defaulting to material 0"});
        } else {
            out.material = emitter.render.material;
        }

        const auto* material = out.material < sanitized_materials.size() ? &sanitized_materials[out.material] : nullptr;
        out.features = compile_features(emitter, material);
        out.kernel = classify_kernel(emitter, material);
        result.effect.storage |= compile_storage(emitter);

        if (out.region.type == ParticleSpawnRegionType::point) {
            out.region.size = {0.0f, 0.0f};
        }

        if (out.material < result.effect.materials.size()) {
            auto& compiled_material = result.effect.materials[out.material];
            const uint16_t max_frame_index = sprite_sheet_max_frame_index(compiled_material.sheet);
            bool clamped_frame_range = false;
            if (compiled_material.sheet.frame_begin > max_frame_index) {
                compiled_material.sheet.frame_begin = max_frame_index;
                clamped_frame_range = true;
            }
            if (compiled_material.sheet.frame_end > max_frame_index) {
                compiled_material.sheet.frame_end = max_frame_index;
                clamped_frame_range = true;
            }
            if (clamped_frame_range) {
                result.warnings.push_back(
                    {static_cast<uint32_t>(i), "sprite-sheet frame range clamped to material dimensions"});
            }
        }

        out.max_particles = std::min(emitter.max_particles, kMaxCompiledParticlesPerEmitter);
        if (out.max_particles != emitter.max_particles) {
            result.warnings.push_back(
                {static_cast<uint32_t>(i), "max_particles exceeds per-emitter runtime cap; clamping"});
        }
        const uint32_t remaining_total = result.effect.max_particles_total < kMaxCompiledParticlesPerEffect
            ? kMaxCompiledParticlesPerEffect - result.effect.max_particles_total
            : 0u;
        if (out.max_particles > remaining_total) {
            out.max_particles = remaining_total;
            result.warnings.push_back(
                {static_cast<uint32_t>(i), "max_particles_total exceeds runtime cap; clamping compiled effect capacity"});
        }
        if (out.max_particles == 0) {
            result.warnings.push_back(
                {static_cast<uint32_t>(i), "max_particles is 0; emitter will not spawn particles"});
        }
        const int32_t clamped_sort_order = std::clamp(
            out.render.sort_order, int32_t{0}, kMaxParticleRenderSortOrder);
        if (clamped_sort_order != out.render.sort_order) {
            out.render.sort_order = clamped_sort_order;
            result.warnings.push_back(
                {static_cast<uint32_t>(i), "sort_order outside [0,255]; clamping"});
        }
        out.emission_rate_track = compile_keyed_scalar_track(emitter.emission.rate_over_time, emitter.emission.rate);
        out.alpha_start_track = compile_keyed_scalar_track(emitter.spawn_over_time.alpha_start, emitter.initial.color.a);
        out.alpha_end_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.alpha_end,
            keyed_end_value_or(emitter.over_life.alpha.keys, emitter.initial.color.a));
        out.lifetime_track = compile_keyed_scalar_track(emitter.spawn_over_time.lifetime, emitter.initial.lifetime.min);
        out.speed_track = compile_keyed_scalar_track(emitter.spawn_over_time.speed, emitter.initial.speed.min);
        out.speed_random_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.speed_random, std::max(0.0f, emitter.initial.speed.max - emitter.initial.speed.min));
        out.mass_track = compile_keyed_scalar_track(emitter.spawn_over_time.mass, emitter.initial.mass);
        out.rotation_rate_track = compile_keyed_scalar_track(emitter.spawn_over_time.rotation_rate, emitter.initial.rotation_rate.min);
        out.spread_track = compile_keyed_scalar_track(emitter.spawn_over_time.spread, emitter.initial.spread_radians);
        const ParticleSpriteSheet base_sheet = material ? material->sheet : ParticleSpriteSheet{};
        out.sheet_frame_begin_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.sheet_frame_begin, static_cast<float>(base_sheet.frame_begin));
        out.sheet_frame_end_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.sheet_frame_end, static_cast<float>(base_sheet.frame_end));
        out.sheet_fps_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.sheet_fps, base_sheet.frames_per_second);
        out.sheet_random_start_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.sheet_random_start, base_sheet.random_start ? 1.0f : 0.0f);
        out.size_start_x_track = compile_keyed_scalar_track(emitter.spawn_over_time.size_start_x, emitter.initial.size_x.min);
        out.size_end_x_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.size_end_x,
            keyed_end_value_or(emitter.over_life.size_x.keys, emitter.initial.size_x.min));
        out.size_start_y_track = compile_keyed_scalar_track(emitter.spawn_over_time.size_start_y, emitter.initial.size_y.min);
        out.size_end_y_track = compile_keyed_scalar_track(
            emitter.spawn_over_time.size_end_y,
            keyed_end_value_or(emitter.over_life.size_y.keys, emitter.initial.size_y.min));
        out.color_start_track = compile_keyed_color_track(emitter.spawn_over_time.color_start, emitter.initial.color);
        out.color_end_track = compile_keyed_color_track(
            emitter.spawn_over_time.color_end,
            keyed_end_value_or(emitter.over_life.color.keys, emitter.initial.color));
        out.alpha_track = compile_scalar_track(emitter.over_life.alpha, emitter.initial.color.a);
        out.size_x_track = compile_scalar_track(emitter.over_life.size_x, emitter.initial.size_x.min);
        out.size_y_track = compile_scalar_track(emitter.over_life.size_y, emitter.initial.size_y.min);
        out.rotation_track = compile_scalar_track(emitter.over_life.rotation, emitter.initial.rotation.min);
        out.color_track = compile_color_track(emitter.over_life.color, emitter.initial.color);
        result.effect.max_particles_total += out.max_particles;
    }

    NW_PROFILE_PLOT("particles/compile/emitters", static_cast<double>(result.effect.emitters.size()));
    NW_PROFILE_PLOT("particles/compile/max_particles_total", static_cast<double>(result.effect.max_particles_total));

    return result;
}

} // namespace nw::render
