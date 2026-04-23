#include "particle_compile.hpp"

#include "../util/profile.hpp"

#include <algorithm>
#include <limits>

namespace nw::render {

namespace {

constexpr size_t kMaxCompiledEmitterCount = static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u;
constexpr size_t kMaxCompiledMaterialCount = static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u;
constexpr uint32_t kGlobalCompileWarningEmitter = std::numeric_limits<uint32_t>::max();

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

CompiledParticleScalarTrack compile_scalar_track(const ParticleCurveF32& curve, float default_value)
{
    if (curve.keys.empty()) {
        return {.keys = {{0.0f, default_value}}};
    }

    return {.keys = curve.keys};
}

CompiledParticleColorTrack compile_color_track(const ParticleGradient& gradient, const glm::vec4& default_value)
{
    if (gradient.keys.empty()) {
        return {.keys = {{0.0f, default_value}}};
    }

    return {.keys = gradient.keys};
}

uint32_t compile_features(const ParticleEmitterDef& emitter, const ParticleMaterialDef* material);

CompiledParticleKeyedScalarTrack compile_keyed_scalar_track(const ParticleCurveF32& curve, float fallback)
{
    if (curve.keys.empty()) {
        return {.keys = {{0.0f, fallback}}};
    }

    return {.keys = curve.keys};
}

CompiledParticleKeyedColorTrack compile_keyed_color_track(const ParticleGradient& gradient, const glm::vec4& fallback)
{
    if (gradient.keys.empty()) {
        return {.keys = {{0.0f, fallback}}};
    }

    return {.keys = gradient.keys};
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
        const uint16_t max_frames = std::max<uint16_t>(
            1, static_cast<uint16_t>(material->sheet.columns * material->sheet.rows));
        const uint16_t frame_begin = material->sheet.frame_begin;
        uint16_t frame_end = std::max(frame_begin, material->sheet.frame_end);
        if (max_frames > 1 && frame_begin == 0 && frame_end == 0) {
            frame_end = static_cast<uint16_t>(max_frames - 1);
        }
        const uint16_t frame_count = static_cast<uint16_t>(frame_end - frame_begin + 1);
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

    if (def.materials.size() > material_count) {
        result.warnings.push_back(
            {kGlobalCompileWarningEmitter, "material count exceeds 16-bit runtime limit; truncating compiled material table"});
    }
    if (def.emitters.size() > emitter_count) {
        result.warnings.push_back(
            {kGlobalCompileWarningEmitter, "emitter count exceeds 16-bit runtime limit; truncating compiled emitter table"});
    }

    for (size_t i = 0; i < material_count; ++i) {
        const auto& material = def.materials[i];
        auto& out = result.effect.materials.emplace_back();
        out.blend = material.blend;
        out.double_sided = material.double_sided;
        out.sheet = material.sheet;
        out.texture = 0;
        out.mesh = 0;
    }

    for (size_t i = 0; i < emitter_count; ++i) {
        NW_PROFILE_SCOPE_N("compile_particle_emitter");
        const auto& emitter = def.emitters[i];
        auto& out = result.effect.emitters.emplace_back();
        NW_PROFILE_TEXT_CSTR(emitter.name.c_str());

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

        const auto* material = out.material < def.materials.size() ? &def.materials[out.material] : nullptr;
        out.features = compile_features(emitter, material);
        out.kernel = classify_kernel(emitter, material);
        result.effect.storage |= compile_storage(emitter);

        if (out.region.type == ParticleSpawnRegionType::point) {
            out.region.size = {0.0f, 0.0f};
        }

        if (out.material < result.effect.materials.size()) {
            auto& compiled_material = result.effect.materials[out.material];
            const uint16_t max_frames = compiled_material.sheet.columns * compiled_material.sheet.rows;
            if (max_frames > 0 && compiled_material.sheet.frame_end >= max_frames) {
                compiled_material.sheet.frame_end = static_cast<uint16_t>(max_frames - 1);
                result.warnings.push_back(
                    {static_cast<uint32_t>(i), "sprite-sheet frame range clamped to material dimensions"});
            }
        }

        out.max_particles = emitter.max_particles;
        out.emission_rate_track = compile_keyed_scalar_track(emitter.emission.rate_over_time, emitter.emission.rate);
        out.alpha_start_track = compile_keyed_scalar_track(emitter.spawn_over_time.alpha_start, emitter.initial.color.a);
        out.alpha_end_track = compile_keyed_scalar_track(emitter.spawn_over_time.alpha_end,
            emitter.over_life.alpha.keys.empty() ? emitter.initial.color.a : emitter.over_life.alpha.keys.back().value);
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
        out.size_end_x_track = compile_keyed_scalar_track(emitter.spawn_over_time.size_end_x,
            emitter.over_life.size_x.keys.empty() ? emitter.initial.size_x.min : emitter.over_life.size_x.keys.back().value);
        out.size_start_y_track = compile_keyed_scalar_track(emitter.spawn_over_time.size_start_y, emitter.initial.size_y.min);
        out.size_end_y_track = compile_keyed_scalar_track(emitter.spawn_over_time.size_end_y,
            emitter.over_life.size_y.keys.empty() ? emitter.initial.size_y.min : emitter.over_life.size_y.keys.back().value);
        out.color_start_track = compile_keyed_color_track(emitter.spawn_over_time.color_start, emitter.initial.color);
        out.color_end_track = compile_keyed_color_track(emitter.spawn_over_time.color_end,
            emitter.over_life.color.keys.empty() ? emitter.initial.color : emitter.over_life.color.keys.back().value);
        out.alpha_track = compile_scalar_track(emitter.over_life.alpha, emitter.initial.color.a);
        out.size_x_track = compile_scalar_track(emitter.over_life.size_x, emitter.initial.size_x.min);
        out.size_y_track = compile_scalar_track(emitter.over_life.size_y, emitter.initial.size_y.min);
        out.rotation_track = compile_scalar_track(emitter.over_life.rotation, emitter.initial.rotation.min);
        out.color_track = compile_color_track(emitter.over_life.color, emitter.initial.color);
        result.effect.max_particles_total += emitter.max_particles;
    }

    NW_PROFILE_PLOT("particles/compile/emitters", static_cast<double>(result.effect.emitters.size()));
    NW_PROFILE_PLOT("particles/compile/max_particles_total", static_cast<double>(result.effect.max_particles_total));

    return result;
}

} // namespace nw::render
