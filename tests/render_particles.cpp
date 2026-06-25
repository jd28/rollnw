#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_json.hpp>
#include <nw/render/particle_render.hpp>
#include <nw/render/particle_system.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

using namespace nw::render;

struct FlatGroundCollisionProvider final : ParticleCollisionProvider {
    ParticleCollisionQuery trace_particle(glm::vec3 from, glm::vec3 to, float /*radius*/) const override
    {
        if ((from.z > 0.0f && to.z > 0.0f) || (from.z < 0.0f && to.z < 0.0f) || from.z == to.z) {
            return {};
        }

        const float t = from.z / (from.z - to.z);
        return ParticleCollisionQuery{
            .hit = true,
            .position = glm::mix(from, to, t),
            .normal = glm::vec3{0.0f, 0.0f, 1.0f},
        };
    }
};

TEST(RenderParticles, CompileClassifiesBasicSpriteEmitter)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 16;
    effect.emitters.push_back(emitter);

    auto result = compile_particle_effect(effect);
    ASSERT_EQ(result.effect.emitters.size(), 1);
    EXPECT_EQ(result.effect.emitters[0].kernel, CompiledParticleKernel::sprite_basic_constant);
    EXPECT_EQ(result.effect.max_particles_total, 16);
    EXPECT_TRUE(result.warnings.empty());
}

TEST(RenderParticles, CompileMarksConstantEmittersAsCold)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 16;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.initial.color = {1.0f, 1.0f, 1.0f, 1.0f};
    effect.emitters.push_back(emitter);

    auto result = compile_particle_effect(effect);
    ASSERT_EQ(result.effect.emitters.size(), 1);
    EXPECT_EQ(result.effect.emitters[0].kernel, CompiledParticleKernel::sprite_basic_constant);
    EXPECT_EQ(result.effect.emitters[0].features, 0u);
}

TEST(RenderParticles, CompileMarksAnimatedEmittersHot)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 2;
    material.sheet.rows = 2;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 3;
    material.sheet.frames_per_second = 8.0f;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 16;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.initial.rotation_rate = {1.0f, 1.0f};
    emitter.over_life.alpha.keys = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    emitter.over_life.size_x.keys = {{0.0f, 1.0f}, {1.0f, 2.0f}};
    effect.emitters.push_back(emitter);

    auto result = compile_particle_effect(effect);
    ASSERT_EQ(result.effect.emitters.size(), 1);
    const uint32_t features = result.effect.emitters[0].features;
    EXPECT_NE(features & CompiledParticleFeature::update_size, 0u);
    EXPECT_NE(features & CompiledParticleFeature::update_color, 0u);
    EXPECT_NE(features & CompiledParticleFeature::update_rotation, 0u);
    EXPECT_NE(features & CompiledParticleFeature::update_frame, 0u);
}

TEST(RenderParticles, CompileMarksImplicitMultiFrameAtlasesAnimated)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 4;
    material.sheet.rows = 4;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 0;
    material.sheet.frames_per_second = 0.0f;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 16;
    emitter.initial.lifetime = {1.0f, 1.0f};
    effect.emitters.push_back(emitter);

    auto result = compile_particle_effect(effect);
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    EXPECT_NE(result.effect.emitters[0].features & CompiledParticleFeature::update_frame, 0u);
}

TEST(RenderParticles, CompileMarksSpawnSampleFeatures)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 2;
    material.sheet.rows = 2;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 3;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 16;
    emitter.spawn_over_time.size_start_x.keys = {{0.0f, 1.0f}, {1.0f, 2.0f}};
    emitter.spawn_over_time.alpha_start.keys = {{0.0f, 1.0f}, {1.0f, 0.5f}};
    emitter.spawn_over_time.lifetime.keys = {{0.0f, 1.0f}, {1.0f, 2.0f}};
    emitter.spawn_over_time.sheet_fps.keys = {{0.0f, 4.0f}, {1.0f, 8.0f}};
    effect.emitters.push_back(emitter);

    auto result = compile_particle_effect(effect);
    ASSERT_EQ(result.effect.emitters.size(), 1);
    const uint32_t features = result.effect.emitters[0].features;
    EXPECT_NE(features & CompiledParticleFeature::sample_spawn_size, 0u);
    EXPECT_NE(features & CompiledParticleFeature::sample_spawn_color, 0u);
    EXPECT_NE(features & CompiledParticleFeature::sample_spawn_motion, 0u);
    EXPECT_NE(features & CompiledParticleFeature::sample_spawn_frame, 0u);
}

TEST(RenderParticles, CompileMarksRequiredStorageOnly)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef basic;
    basic.render.material = 0;
    basic.max_particles = 4;
    effect.emitters.push_back(basic);

    ParticleEmitterDef bezier;
    bezier.render.material = 0;
    bezier.targeting.mode = ParticleTargetingMode::point_bezier;
    bezier.simulation_space = ParticleSimulationSpace::spawn_attached;
    bezier.max_particles = 4;
    effect.emitters.push_back(bezier);

    ParticleEmitterDef beam;
    beam.render.material = 0;
    beam.emission.mode = ParticleEmissionMode::beam_continuous;
    beam.targeting.mode = ParticleTargetingMode::beam_lightning;
    beam.max_particles = 1;
    effect.emitters.push_back(beam);

    auto result = compile_particle_effect(effect);
    const uint32_t storage = result.effect.storage;
    EXPECT_NE(storage & CompiledParticleStorage::bezier, 0u);
    EXPECT_NE(storage & CompiledParticleStorage::beams, 0u);
    EXPECT_NE(storage & CompiledParticleStorage::attachment, 0u);
}

TEST(RenderParticles, CreateSystemReservesCompiledCapacity)
{
    CompiledParticleEffect effect;
    effect.emitters.push_back(CompiledParticleEmitter{});
    effect.max_particles_total = 32;

    auto system = create_particle_system(effect);
    EXPECT_EQ(system.emitters.size(), 1);
    EXPECT_EQ(system.particles.core.position.capacity(), 32);
    EXPECT_EQ(system.particles.core.age.capacity(), 32);
}

TEST(RenderParticles, CreateSystemSkipsUnusedSidecarReserves)
{
    CompiledParticleEffect effect;
    effect.emitters.push_back(CompiledParticleEmitter{});
    effect.max_particles_total = 32;

    auto system = create_particle_system(effect);
    EXPECT_FALSE(system.particles.has_bezier);
    EXPECT_FALSE(system.particles.has_beams);
    EXPECT_FALSE(system.particles.has_attachment);
    EXPECT_EQ(system.particles.bezier.source_position.capacity(), 0);
    EXPECT_EQ(system.particles.beams.source_position.capacity(), 0);
    EXPECT_EQ(system.particles.attachment.anchor_position.capacity(), 0);
}

TEST(RenderParticles, ApplyCompiledAttachmentDefaultsSetsEmitterPlacement)
{
    ParticleEffectDef effect;
    ParticleEmitterDef emitter;
    emitter.attachment.has_default_transform = true;
    emitter.attachment.default_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f});
    emitter.attachment.has_default_target_offset = true;
    emitter.attachment.default_target_offset = glm::vec3{4.0f, 5.0f, 6.0f};
    effect.emitters.push_back(std::move(emitter));

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    apply_particle_attachment_defaults(system);

    ASSERT_EQ(system.emitters.size(), 1u);
    EXPECT_EQ(glm::vec3(system.emitters[0].world_transform[3]), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(system.emitters[0].prev_world_pos, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(system.emitters[0].target_point, glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST(RenderParticles, KillParticleRemovesParticleImmediately)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    trigger_particle_emitter(system, 0);
    tick_particle_system(system, 0.016f);
    ASSERT_EQ(system.particles.core.age.size(), 2u);
    ASSERT_EQ(system.live_particles_per_emitter[0], 2u);

    kill_particle(system, 0);
    EXPECT_EQ(system.particles.core.age.size(), 1u);
    EXPECT_EQ(system.live_particles_per_emitter[0], 1u);

    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].count, 1u);
}

TEST(RenderParticles, CompileTruncatesMaterialTablesBeyond16BitRuntimeRange)
{
    ParticleEffectDef effect;
    effect.materials.resize(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 2u);

    ParticleEmitterDef emitter;
    emitter.render.material = static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1u;
    emitter.max_particles = 1;
    effect.emitters.push_back(emitter);

    auto result = compile_particle_effect(effect);
    EXPECT_EQ(result.effect.materials.size(), static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u);
    EXPECT_EQ(result.effect.emitters[0].material, 0u);

    const auto truncation_warning = std::find_if(result.warnings.begin(), result.warnings.end(), [](const auto& warning) {
        return warning.message == "material count exceeds 16-bit runtime limit; truncating compiled material table";
    });
    EXPECT_NE(truncation_warning, result.warnings.end());

    const auto fallback_warning = std::find_if(result.warnings.begin(), result.warnings.end(), [](const auto& warning) {
        return warning.message == "material index exceeds compiled range; defaulting to material 0";
    });
    EXPECT_NE(fallback_warning, result.warnings.end());
}

TEST(RenderParticles, CompileTruncatesEmitterTablesBeyond16BitRuntimeRange)
{
    ParticleEffectDef effect;
    effect.emitters.resize(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 2u);

    auto result = compile_particle_effect(effect);
    EXPECT_EQ(result.effect.emitters.size(), static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1u);

    const auto truncation_warning = std::find_if(result.warnings.begin(), result.warnings.end(), [](const auto& warning) {
        return warning.message == "emitter count exceeds 16-bit runtime limit; truncating compiled emitter table";
    });
    EXPECT_NE(truncation_warning, result.warnings.end());
}

TEST(RenderParticles, CompileWarnsWhenEmitterParticleCapIsZero)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.rate = 1.0f;
    effect.emitters.push_back(emitter);

    auto result = compile_particle_effect(effect);
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    EXPECT_EQ(result.effect.emitters[0].max_particles, 0u);
    EXPECT_EQ(result.effect.max_particles_total, 0u);

    const auto warning = std::find_if(result.warnings.begin(), result.warnings.end(), [](const auto& item) {
        return item.emitter == 0u
            && item.message == "max_particles is 0; emitter will not spawn particles";
    });
    EXPECT_NE(warning, result.warnings.end());
}

TEST(RenderParticles, TickEnforcesPerEmitterParticleCap)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef first;
    first.render.material = 0;
    first.emission.mode = ParticleEmissionMode::continuous;
    first.emission.metric = ParticleSpawnMetric::per_second;
    first.emission.rate = 8.0f;
    first.initial.lifetime = {10.0f, 10.0f};
    first.max_particles = 2;
    effect.emitters.push_back(first);

    ParticleEmitterDef second = first;
    second.max_particles = 8;
    effect.emitters.push_back(second);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);

    ASSERT_EQ(system.particles.core.age.size(), 6u);
    EXPECT_EQ(system.live_particles_per_emitter[0], 2u);
    EXPECT_EQ(system.live_particles_per_emitter[1], 4u);
}

TEST(RenderParticles, TickClampsNegativeEmissionRate)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = -430.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 1.0f);

    EXPECT_TRUE(system.particles.core.age.empty());
    EXPECT_EQ(system.live_particles_per_emitter[0], 0u);
}

TEST(RenderParticles, TickHandlesEmittersWithoutCompiledMaterials)
{
    ParticleEffectDef effect;
    ParticleEmitterDef emitter;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect);
    ASSERT_FALSE(compiled.warnings.empty());

    auto system = create_particle_system(compiled.effect);
    tick_particle_system(system, 0.5f);

    ASSERT_EQ(system.particles.core.age.size(), 1u);
    EXPECT_EQ(system.particles.core.frame[0], 0u);

    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].sheet.frame_begin, 0u);
    EXPECT_EQ(packets[0].sheet.frame_end, 0u);
    EXPECT_EQ(packets[0].blend, ParticleBlendMode::alpha);
}

TEST(RenderParticles, TickSpawnsAndUpdatesContinuousEmitter)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.speed = {2.0f, 2.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 32;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.age.size(), 2);
    EXPECT_FLOAT_EQ(system.particles.core.age[0], 0.25f);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].z, 0.5f);

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.age.size(), 5);
    EXPECT_FLOAT_EQ(system.particles.core.age[0], 0.5f);
}

TEST(RenderParticles, TickAppliesExternalWindToOptInEmitters)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.affected_by_wind = true;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f, ParticleExternalForces{.wind = glm::vec3{2.0f, 0.0f, 0.0f}});
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    EXPECT_NEAR(system.particles.core.velocity[0].x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(system.particles.core.position[0].x, 0.5f, 1.0e-5f);
}

TEST(RenderParticles, TickAppliesCollisionBounceThroughSimulationContext)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.rate = 1.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.initial.size_x = {0.5f, 0.5f};
    emitter.initial.size_y = {0.5f, 0.5f};
    emitter.initial.mass = 0.0f;
    emitter.collision.enabled = true;
    emitter.collision.bounce = true;
    emitter.collision.bounce_coefficient = 0.5f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].world_transform[3] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    tick_particle_system(system, 1.0f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    system.particles.core.velocity[0] = glm::vec3{0.0f, 0.0f, -2.0f};

    FlatGroundCollisionProvider collision;
    tick_particle_system(system, 0.75f, ParticleSimulationContext{
                                            .collision = &collision,
                                        });

    ASSERT_EQ(system.particles.core.age.size(), 1u);
    EXPECT_NEAR(system.particles.core.position[0].z, 0.0f, 1.0e-5f);
    EXPECT_NEAR(system.particles.core.velocity[0].z, 1.0f, 1.0e-5f);
}

TEST(RenderParticles, TickConsumesQueuedEventBursts)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 5.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 32;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    trigger_particle_emitter(system, 0);
    tick_particle_system(system, 0.016f);
    ASSERT_EQ(system.particles.core.age.size(), 5u);

    trigger_particle_emitter(system, 0, 2);
    tick_particle_system(system, 0.016f);
    ASSERT_EQ(system.particles.core.age.size(), 15u);
    EXPECT_EQ(system.emitters[0].pending_bursts, 0u);
}

TEST(RenderParticles, TickOutputsForceEventsForBurstEmitters)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 2.0f;
    emitter.force_event.radius = 0.2f;
    emitter.force_event.length = 1.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 16;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    trigger_particle_emitter(system, 0, 2);
    tick_particle_system(system, 0.016f);

    const auto events = get_particle_force_events(system);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].emitter_id, 0u);
    EXPECT_FLOAT_EQ(events[0].radius, 0.2f);
    EXPECT_FLOAT_EQ(events[0].length, 1.0f);
    EXPECT_EQ(system.particles.core.age.size(), 4u);

    tick_particle_system(system, 0.016f);
    EXPECT_TRUE(get_particle_force_events(system).empty());
}

TEST(RenderParticles, TickEmitsSingleShotOnceWhenNotLooping)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::single_shot;
    emitter.emission.looping = false;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.016f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    EXPECT_FALSE(system.emitters[0].active);
}

TEST(RenderParticles, TickLoopsSingleShotOnEmissionCadence)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::single_shot;
    emitter.emission.looping = true;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {5.0f, 5.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);

    tick_particle_system(system, 0.39f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);

    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.age.size(), 2u);
}

TEST(RenderParticles, BuildRenderPacketsGroupsByEmitterAndMaterial)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});
    ParticleMaterialDef cutout;
    cutout.blend = ParticleBlendMode::cutout;
    effect.materials.push_back(cutout);

    ParticleEmitterDef a;
    a.render.material = 0;
    a.emission.mode = ParticleEmissionMode::continuous;
    a.emission.rate = 4.0f;
    a.initial.lifetime = {1.0f, 1.0f};
    a.max_particles = 8;
    effect.emitters.push_back(a);

    ParticleEmitterDef b;
    b.render.material = 1;
    b.emission.mode = ParticleEmissionMode::continuous;
    b.emission.rate = 4.0f;
    b.initial.lifetime = {1.0f, 1.0f};
    b.max_particles = 8;
    effect.emitters.push_back(b);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 2);
    EXPECT_EQ(packets[0].material, 0);
    EXPECT_EQ(packets[1].material, 1);
    EXPECT_EQ(packets[0].blend, ParticleBlendMode::alpha);
    EXPECT_EQ(packets[1].blend, ParticleBlendMode::cutout);
    EXPECT_TRUE(packets[0].transparent);
    EXPECT_FALSE(packets[1].transparent);
}

TEST(RenderParticles, BuildRenderPacketsHonorsEmitterSortOrder)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef low;
    low.render.material = 0;
    low.render.sort_order = 0;
    low.emission.mode = ParticleEmissionMode::continuous;
    low.emission.rate = 2.0f;
    low.initial.lifetime = {2.0f, 2.0f};
    low.max_particles = 4;
    effect.emitters.push_back(low);

    ParticleEmitterDef high;
    high.render.material = 0;
    high.render.sort_order = 5;
    high.emission.mode = ParticleEmissionMode::continuous;
    high.emission.rate = 2.0f;
    high.initial.lifetime = {2.0f, 2.0f};
    high.max_particles = 4;
    effect.emitters.push_back(high);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 2u);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[0].begin]], 0u);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[1].begin]], 1u);
}

TEST(RenderParticles, BuildRenderPacketsDoesNotPermuteSimulationStorage)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef high;
    high.render.material = 0;
    high.render.sort_order = 5;
    high.emission.mode = ParticleEmissionMode::continuous;
    high.emission.rate = 2.0f;
    high.initial.lifetime = {2.0f, 2.0f};
    high.max_particles = 4;
    effect.emitters.push_back(high);

    ParticleEmitterDef low = high;
    low.render.sort_order = 0;
    effect.emitters.push_back(low);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.emitter_id.size(), 2u);
    const auto emitter_ids_before = system.particles.core.emitter_id;
    const auto ages_before = system.particles.core.age;

    const auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 2u);
    EXPECT_EQ(system.particles.core.emitter_id, emitter_ids_before);
    EXPECT_EQ(system.particles.core.age, ages_before);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[0].begin]], 1u);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[1].begin]], 0u);
}

TEST(RenderParticles, RenderPacketIndicesMapRangesThroughSortOrder)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef high;
    high.render.material = 0;
    high.render.sort_order = 5;
    high.emission.mode = ParticleEmissionMode::continuous;
    high.emission.rate = 2.0f;
    high.initial.lifetime = {2.0f, 2.0f};
    high.max_particles = 4;
    effect.emitters.push_back(high);

    ParticleEmitterDef low = high;
    low.render.sort_order = 0;
    effect.emitters.push_back(low);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    const auto packets = build_particle_render_packets(system);

    ASSERT_EQ(system.particles.core.emitter_id.size(), 2u);
    ASSERT_EQ(packets.size(), 2u);

    const auto first_packet_indices = particle_render_packet_indices(system, packets[0]);
    ASSERT_EQ(first_packet_indices.size(), 1u);
    EXPECT_EQ(packets[0].begin, 0u);
    EXPECT_EQ(first_packet_indices[0], 1u);
    EXPECT_EQ(system.particles.core.emitter_id[first_packet_indices[0]], 1u);

    const auto second_packet_indices = particle_render_packet_indices(system, packets[1]);
    ASSERT_EQ(second_packet_indices.size(), 1u);
    EXPECT_EQ(second_packet_indices[0], 0u);
    EXPECT_EQ(system.particles.core.emitter_id[second_packet_indices[0]], 0u);
}

TEST(RenderParticles, RenderPacketIndicesClampStaleRanges)
{
    ParticleSystemInstance system;
    system.sort_order = {5u, 3u, 7u};

    ParticleRenderPacket packet;
    packet.begin = 1;
    packet.count = 8;

    const auto indices = particle_render_packet_indices(system, packet);
    ASSERT_EQ(indices.size(), 2u);
    EXPECT_EQ(indices[0], 3u);
    EXPECT_EQ(indices[1], 7u);

    packet.begin = 9;
    EXPECT_TRUE(particle_render_packet_indices(system, packet).empty());
}

TEST(RenderParticles, BuildRenderPacketsMergeCompatibleEmitters)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef a;
    a.render.material = 0;
    a.emission.mode = ParticleEmissionMode::continuous;
    a.emission.rate = 2.0f;
    a.initial.lifetime = {2.0f, 2.0f};
    a.max_particles = 4;
    effect.emitters.push_back(a);

    ParticleEmitterDef b = a;
    effect.emitters.push_back(b);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    const auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].count, 2u);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[0].begin]], 0u);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[0].begin + 1]], 1u);
}

TEST(RenderParticles, BuildRenderPacketsKeepLinkedChainsPartitionedPerEmitter)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef chain;
    chain.render.material = 0;
    chain.render.mode = ParticleRenderMode::linked_chain;
    chain.targeting.mode = ParticleTargetingMode::none;
    chain.emission.mode = ParticleEmissionMode::continuous;
    chain.emission.rate = 2.0f;
    chain.initial.lifetime = {2.0f, 2.0f};
    chain.max_particles = 4;
    effect.emitters.push_back(chain);
    effect.emitters.push_back(chain);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    const auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 2u);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[0].begin]], 0u);
    EXPECT_EQ(system.particles.core.emitter_id[system.sort_order[packets[1].begin]], 1u);
}

TEST(RenderParticles, BuildRenderPacketsExposeMeshModeForChunkEmitters)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.mesh = "PLC_Chunk_W01";
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    ASSERT_EQ(compiled.emitters[0].kernel, CompiledParticleKernel::mesh_basic);
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].mode, ParticleRenderMode::mesh);
}

TEST(RenderParticles, BuildRenderPacketsCarryRenderOpacityAndBlur)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::stretched;
    emitter.render.opacity_scale = 0.35f;
    emitter.render.blur_length = 1.75f;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 1u);
    EXPECT_FLOAT_EQ(packets[0].opacity_scale, 0.35f);
    EXPECT_FLOAT_EQ(packets[0].blur_length, 1.75f);
}

TEST(RenderParticles, BuildRenderPacketsCarryAmbientTintFlag)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.tint_to_scene_ambient = true;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    auto packets = build_particle_render_packets(system);

    ASSERT_EQ(packets.size(), 1u);
    EXPECT_TRUE(packets[0].tint_to_scene_ambient);
}

TEST(RenderParticles, TickEvaluatesCompiledOverLifeTracks)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {2.0f, 2.0f};
    emitter.initial.rotation = {0.0f, 0.0f};
    emitter.initial.color = {1.0f, 0.0f, 0.0f, 1.0f};
    emitter.over_life.size_x.keys = {{0.0f, 1.0f}, {1.0f, 3.0f}};
    emitter.over_life.size_y.keys = {{0.0f, 2.0f}, {1.0f, 4.0f}};
    emitter.over_life.alpha.keys = {{0.0f, 1.0f}, {1.0f, 0.5f}};
    emitter.over_life.color.keys = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.age.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.normalized_age[0], 0.5f);
    EXPECT_FLOAT_EQ(system.particles.core.size_x[0], 2.0f);
    EXPECT_FLOAT_EQ(system.particles.core.size_y[0], 3.0f);
    EXPECT_EQ(system.particles.core.color_rgba8[0], 0xBF800080u);
}

TEST(RenderParticles, TickEvaluatesThreeStageOverLifeTracks)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.initial.color = {1.0f, 0.0f, 0.0f, 1.0f};
    emitter.over_life.alpha.keys = {{0.0f, 1.0f}, {0.25f, 1.0f}, {1.0f, 1.0f}};
    emitter.over_life.size_x.keys = {{0.0f, 1.0f}, {0.5f, 3.0f}, {1.0f, 2.0f}};
    emitter.over_life.color.keys = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {0.5f, {0.0f, 1.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.size_x[0], 3.0f);
    EXPECT_EQ(system.particles.core.color_rgba8[0], 0xFF00FF00u);
}

TEST(RenderParticles, CompileSortsAuthoredParticleTrackKeysByTime)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.over_life.size_x.keys = {{1.0f, 4.0f}, {0.0f, 1.0f}, {0.5f, 2.0f}};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    const auto& size_keys = compiled.emitters[0].size_x_track.keys;
    ASSERT_EQ(size_keys.size(), 3u);
    EXPECT_FLOAT_EQ(size_keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(size_keys[1].time, 0.5f);
    EXPECT_FLOAT_EQ(size_keys[2].time, 1.0f);
    ASSERT_EQ(compiled.emitters[0].size_end_x_track.keys.size(), 1u);
    EXPECT_FLOAT_EQ(compiled.emitters[0].size_end_x_track.keys[0].value, 4.0f);
}

TEST(RenderParticles, TickSpawnsPerDistanceAlongEmitterPath)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_distance;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {10.0f, 10.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].prev_world_pos = {0.0f, 0.0f, 0.0f};
    system.emitters[0].world_transform[3] = {2.0f, 0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.position.size(), 4);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 0.4f);
    EXPECT_FLOAT_EQ(system.particles.core.position[1].x, 0.8f);
    EXPECT_FLOAT_EQ(system.particles.core.position[2].x, 1.2f);
    EXPECT_FLOAT_EQ(system.particles.core.position[3].x, 1.6f);
}

TEST(RenderParticles, TickInterpolatesPerSecondSpawnPositions)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 4.0f;
    emitter.initial.lifetime = {10.0f, 10.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].prev_world_pos = {0.0f, 0.0f, 0.0f};
    system.emitters[0].world_transform[3] = {1.0f, 0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(system.particles.core.position[1].x, 2.0f / 3.0f);
}

TEST(RenderParticles, TickRandomizesRectSpawnPositionsWithinBounds)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.region.type = ParticleSpawnRegionType::rect;
    emitter.region.size = {100.0f, 50.0f};
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 4.0f;
    emitter.initial.lifetime = {10.0f, 10.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);

    const auto& a = system.particles.core.position[0];
    const auto& b = system.particles.core.position[1];
    EXPECT_GE(a.x, -0.5f);
    EXPECT_LE(a.x, 0.5f);
    EXPECT_GE(a.y, -0.25f);
    EXPECT_LE(a.y, 0.25f);
    EXPECT_GE(b.x, -0.5f);
    EXPECT_LE(b.x, 0.5f);
    EXPECT_GE(b.y, -0.25f);
    EXPECT_LE(b.y, 0.25f);
    EXPECT_TRUE(a.x != b.x || a.y != b.y);
}

TEST(RenderParticles, TickAppliesSpreadToBasicSpriteVelocity)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {1.0f, 1.0f};
    emitter.initial.spread_radians = 0.5f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_NE(system.particles.core.position[0].x, 0.0f);
}

TEST(RenderParticles, TickAppliesMassAccelerationToBasicSprites)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef falling;
    falling.render.material = 0;
    falling.emission.mode = ParticleEmissionMode::continuous;
    falling.emission.metric = ParticleSpawnMetric::per_second;
    falling.emission.rate = 2.0f;
    falling.initial.lifetime = {2.0f, 2.0f};
    falling.initial.speed = {0.0f, 0.0f};
    falling.initial.mass = 2.0f;
    falling.max_particles = 8;
    effect.emitters.push_back(falling);

    ParticleEmitterDef rising = falling;
    rising.initial.mass = -2.0f;
    effect.emitters.push_back(rising);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);
    EXPECT_LT(system.particles.core.position[0].z, 0.0f);
    EXPECT_GT(system.particles.core.position[1].z, 0.0f);
}

TEST(RenderParticles, TickMovesEmitterAttachedParticlesWithEmitterDelta)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 0.0f);

    system.emitters[0].world_transform[3] = {1.0f, 0.0f, 0.0f, 1.0f};
    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 1.0f);
}

TEST(RenderParticles, TickKeepsEmitterAttachedZeroSpeedParticlesClusteredAtCurrentEmitter)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 32;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    for (int i = 0; i < 4; ++i) {
        system.emitters[0].world_transform[3] = glm::vec4(static_cast<float>(i + 1), 0.0f, 0.0f, 1.0f);
        tick_particle_system(system, 0.1f);
    }

    ASSERT_FALSE(system.particles.core.position.empty());
    float min_x = system.particles.core.position.front().x;
    float max_x = min_x;
    for (const auto& position : system.particles.core.position) {
        min_x = std::min(min_x, position.x);
        max_x = std::max(max_x, position.x);
    }

    EXPECT_NEAR(min_x, 4.0f, 1.0e-5f);
    EXPECT_NEAR(max_x, 4.0f, 1.0e-5f);
}

TEST(RenderParticles, TickAddsVelocityInheritanceAtSpawn)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.initial.velocity_inheritance = 1.0f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].prev_world_pos = {0.0f, 0.0f, 0.0f};
    system.emitters[0].world_transform[3] = {1.0f, 0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.velocity[0].x, 2.0f);
}

TEST(RenderParticles, TickKeepsSpawnAttachedParticlesAtSpawnAnchor)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.simulation_space = ParticleSimulationSpace::spawn_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 0.0f);

    system.emitters[0].world_transform[3] = {1.0f, 0.0f, 0.0f, 1.0f};
    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 0.0f);
    EXPECT_FLOAT_EQ(system.particles.core.position[1].x, 0.5f);
}

TEST(RenderParticles, TickMovesLocalParticlesWithCurrentEmitterTransform)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.simulation_space = ParticleSimulationSpace::local;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.region.type = ParticleSpawnRegionType::rect;
    emitter.region.size = {100.0f, 0.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    const float local_offset_x = system.particles.attachment.local_position[0].x;

    system.emitters[0].world_transform[3] = {1.0f, 0.0f, 0.0f, 1.0f};
    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 1.0f + local_offset_x);
}

TEST(RenderParticles, TickKeepsSpawnAttachedGravityParticlesAtSpawnAnchor)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.simulation_space = ParticleSimulationSpace::spawn_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.targeting.mode = ParticleTargetingMode::point_gravity;
    emitter.targeting.gravity = 0.0f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);

    system.emitters[0].world_transform[3] = {1.0f, 0.0f, 0.0f, 1.0f};
    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 0.0f);
    EXPECT_FLOAT_EQ(system.particles.core.position[1].x, 0.5f);
}

TEST(RenderParticles, TickMovesLocalBezierParticlesWithEmitterDelta)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.simulation_space = ParticleSimulationSpace::local;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.targeting.mode = ParticleTargetingMode::point_bezier;
    emitter.targeting.transition_factor = 1.0f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].z, 0.5f);

    system.emitters[0].world_transform[3] = {1.0f, 0.0f, 0.0f, 1.0f};
    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 2);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].x, 1.0f);
}

TEST(RenderParticles, TickUsesEmitterTimeRateCurve)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 0.0f;
    emitter.emission.rate_over_time.keys = {{0.0f, 0.0f}, {0.5f, 4.0f}, {1.0f, 0.0f}};
    emitter.initial.lifetime = {10.0f, 10.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.25f);
    EXPECT_EQ(system.particles.core.position.size(), 0);

    tick_particle_system(system, 0.25f);
    EXPECT_EQ(system.particles.core.position.size(), 1);

    tick_particle_system(system, 0.25f);
    EXPECT_EQ(system.particles.core.position.size(), 2);
}

TEST(RenderParticles, TickSamplesSpawnAnimatedSizeAndColor)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.initial.color = {1.0f, 1.0f, 1.0f, 1.0f};
    emitter.spawn_over_time.size_start_x.keys = {{0.0f, 1.0f}, {1.0f, 3.0f}};
    emitter.spawn_over_time.size_end_x.keys = {{0.0f, 2.0f}, {1.0f, 4.0f}};
    emitter.spawn_over_time.alpha_start.keys = {{0.0f, 1.0f}, {1.0f, 0.5f}};
    emitter.spawn_over_time.alpha_end.keys = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    emitter.spawn_over_time.color_start.keys = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
    };
    emitter.spawn_over_time.color_end.keys = {
        {0.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 0.0f, 1.0f}},
    };
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.size_x_begin[0], 2.0f);
    EXPECT_FLOAT_EQ(system.particles.core.size_x_end[0], 3.0f);
    EXPECT_EQ(system.particles.core.color_rgba8[0], 0x9F408080u);
}

TEST(RenderParticles, TickSamplesSpawnAnimatedMotionAndRotation)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.initial.mass = 0.0f;
    emitter.initial.rotation_rate = {0.0f, 0.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.spawn_over_time.speed.keys = {{0.0f, 1.0f}, {1.0f, 3.0f}};
    emitter.spawn_over_time.speed_random.keys = {{0.0f, 0.0f}, {1.0f, 0.0f}};
    emitter.spawn_over_time.lifetime.keys = {{0.0f, 1.0f}, {1.0f, 2.0f}};
    emitter.spawn_over_time.mass.keys = {{0.0f, -0.25f}, {1.0f, -0.75f}};
    emitter.spawn_over_time.rotation_rate.keys = {{0.0f, 0.5f}, {1.0f, 1.5f}};
    emitter.spawn_over_time.spread.keys = {{0.0f, 0.0f}, {1.0f, 0.5f}};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1u);
    EXPECT_FLOAT_EQ(system.particles.core.lifetime[0], 1.5f);
    EXPECT_NEAR(glm::length(system.particles.core.velocity[0]), 2.25f, 1.0e-2f);
    EXPECT_NE(system.particles.core.velocity[0].x, 0.0f);
    EXPECT_FLOAT_EQ(system.particles.core.mass[0], -0.5f);
    EXPECT_FLOAT_EQ(system.particles.core.rotation_rate[0], 1.0f);
}

TEST(RenderParticles, TickAdvancesSpriteSheetFrames)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 2;
    material.sheet.rows = 2;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 3;
    material.sheet.frames_per_second = 4.0f;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {10.0f, 10.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.frame.size(), 1u);
    EXPECT_EQ(system.particles.core.frame[0], 2u);

    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].sheet.frame_end, 3u);
    EXPECT_FLOAT_EQ(packets[0].sheet.frames_per_second, 4.0f);
}

TEST(RenderParticles, TickAssignsRandomSpriteSheetStartFrames)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 2;
    material.sheet.rows = 2;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 3;
    material.sheet.frames_per_second = 0.0f;
    material.sheet.random_start = true;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 4.0f;
    emitter.initial.lifetime = {10.0f, 10.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 1.0f);
    ASSERT_EQ(system.particles.core.frame.size(), 4u);
    bool saw_variation = false;
    for (size_t i = 0; i < system.particles.core.frame.size(); ++i) {
        EXPECT_GE(system.particles.core.frame[i], 0u);
        EXPECT_LE(system.particles.core.frame[i], 3u);
        if (system.particles.core.frame[i] != system.particles.core.frame[0]) {
            saw_variation = true;
        }
    }
    EXPECT_TRUE(saw_variation);
}

TEST(RenderParticles, TickAdvancesImplicitMultiFrameAtlasOverLifetime)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 2;
    material.sheet.rows = 2;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 0;
    material.sheet.frames_per_second = 0.0f;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 1.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    trigger_particle_emitter(system, 0, 1);
    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.frame.size(), 1u);
    EXPECT_EQ(system.particles.core.frame[0], 2u);
}

TEST(RenderParticles, TickEventBurstWithZeroRateSpawnsNoParticles)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 0.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    trigger_particle_emitter(system, 0, 1);
    tick_particle_system(system, 0.016f);

    EXPECT_TRUE(system.particles.core.age.empty());
    EXPECT_EQ(system.live_particles_per_emitter[0], 0u);
}

TEST(RenderParticles, TickUsesEmitterTimeSpriteSheetTracks)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 4;
    material.sheet.rows = 4;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 3;
    material.sheet.frames_per_second = 4.0f;
    material.sheet.random_start = false;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 4.0f;
    emitter.initial.lifetime = {10.0f, 10.0f};
    emitter.spawn_over_time.sheet_frame_begin.keys = {{0.0f, 4.0f}, {1.0f, 4.0f}};
    emitter.spawn_over_time.sheet_frame_end.keys = {{0.0f, 7.0f}, {1.0f, 7.0f}};
    emitter.spawn_over_time.sheet_fps.keys = {{0.0f, 8.0f}, {1.0f, 8.0f}};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.frame.size(), 1u);
    EXPECT_EQ(system.particles.core.frame[0], 6u);

    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].sheet.frame_begin, 4u);
    EXPECT_EQ(packets[0].sheet.frame_end, 7u);
    EXPECT_FLOAT_EQ(packets[0].sheet.frames_per_second, 8.0f);
}

TEST(RenderParticles, SpriteSheetFrameRectUsesAbsoluteAtlasFrame)
{
    ParticleSpriteSheet single_frame_sheet;
    single_frame_sheet.columns = 2;
    single_frame_sheet.rows = 2;
    single_frame_sheet.frame_begin = 1;
    single_frame_sheet.frame_end = 1;

    const auto single_frame_rect = particle_sprite_sheet_frame_rect(single_frame_sheet, 1);
    EXPECT_EQ(single_frame_rect.column, 1u);
    EXPECT_EQ(single_frame_rect.row, 0u);
    EXPECT_FLOAT_EQ(single_frame_rect.u0, 0.5f);
    EXPECT_FLOAT_EQ(single_frame_rect.v0, 0.0f);
    EXPECT_FLOAT_EQ(single_frame_rect.u1, 1.0f);
    EXPECT_FLOAT_EQ(single_frame_rect.v1, 0.5f);

    const auto flipped_single_frame_rect = particle_sprite_sheet_frame_rect(single_frame_sheet, 1, nullptr, true);
    EXPECT_EQ(flipped_single_frame_rect.column, 1u);
    EXPECT_EQ(flipped_single_frame_rect.row, 1u);
    EXPECT_FLOAT_EQ(flipped_single_frame_rect.u0, 0.5f);
    EXPECT_FLOAT_EQ(flipped_single_frame_rect.v0, 0.5f);
    EXPECT_FLOAT_EQ(flipped_single_frame_rect.u1, 1.0f);
    EXPECT_FLOAT_EQ(flipped_single_frame_rect.v1, 1.0f);

    ParticleSpriteSheet keyed_sheet;
    keyed_sheet.columns = 4;
    keyed_sheet.rows = 4;
    keyed_sheet.frame_begin = 4;
    keyed_sheet.frame_end = 7;

    const auto keyed_rect = particle_sprite_sheet_frame_rect(keyed_sheet, 6);
    EXPECT_EQ(keyed_rect.column, 2u);
    EXPECT_EQ(keyed_rect.row, 1u);
    EXPECT_FLOAT_EQ(keyed_rect.u0, 0.5f);
    EXPECT_FLOAT_EQ(keyed_rect.v0, 0.25f);
    EXPECT_FLOAT_EQ(keyed_rect.u1, 0.75f);
    EXPECT_FLOAT_EQ(keyed_rect.v1, 0.5f);

    const auto clamped_rect = particle_sprite_sheet_frame_rect(single_frame_sheet, 99);
    EXPECT_EQ(clamped_rect.column, 1u);
    EXPECT_EQ(clamped_rect.row, 1u);
    EXPECT_FLOAT_EQ(clamped_rect.u0, 0.5f);
    EXPECT_FLOAT_EQ(clamped_rect.v0, 0.5f);
    EXPECT_FLOAT_EQ(clamped_rect.u1, 1.0f);
    EXPECT_FLOAT_EQ(clamped_rect.v1, 1.0f);
}

TEST(RenderParticles, TickAppliesGravityTargeting)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.targeting.mode = ParticleTargetingMode::point_gravity;
    emitter.targeting.gravity = 4.0f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    ASSERT_EQ(compiled.emitters[0].kernel, CompiledParticleKernel::sprite_target_gravity);

    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].z, 1.0f);
}

TEST(RenderParticles, TickAppliesSpreadToGravityTargeting)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 4.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {1.0f, 1.0f};
    emitter.initial.spread_radians = glm::radians(25.0f);
    emitter.targeting.mode = ParticleTargetingMode::point_gravity;
    emitter.targeting.gravity = 0.0f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    ASSERT_EQ(compiled.emitters[0].kernel, CompiledParticleKernel::sprite_target_gravity);

    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 4.0f};
    trigger_particle_emitter(system, 0, 1);

    tick_particle_system(system, 0.01f);
    ASSERT_EQ(system.particles.core.position.size(), 4u);

    bool has_lateral_motion = false;
    for (size_t i = 0; i < system.particles.core.velocity.size(); ++i) {
        const auto& velocity = system.particles.core.velocity[i];
        EXPECT_GT(velocity.z, 0.0f);
        has_lateral_motion = has_lateral_motion
            || std::abs(velocity.x) > 1.0e-4f
            || std::abs(velocity.y) > 1.0e-4f;
    }
    EXPECT_TRUE(has_lateral_motion);
}

TEST(RenderParticles, TickGravityTargetingDragDampsVelocity)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 1.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {1.0f, 1.0f};
    emitter.targeting.mode = ParticleTargetingMode::point_gravity;
    emitter.targeting.gravity = 0.0f;
    emitter.targeting.drag = 1.0f;
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    ASSERT_EQ(compiled.emitters[0].kernel, CompiledParticleKernel::sprite_target_gravity);

    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 4.0f};
    trigger_particle_emitter(system, 0, 1);

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.velocity.size(), 1u);
    EXPECT_LT(glm::length(system.particles.core.velocity[0]), 1.0f);
}

TEST(RenderParticles, TickPulsedAttachedVelocityAlignedEmitterLeavesPacketsAlongPath)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.blend = ParticleBlendMode::additive;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::velocity_aligned;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.trigger_on_effect_events = true;
    emitter.emission.effect_event_period = 0.1f;
    emitter.emission.rate = 20.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 16;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    system.emitters[0].prev_world_pos = glm::vec3{0.0f, 0.0f, 0.0f};
    system.emitters[0].world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f});
    trigger_particle_emitter(system, 0, 1);
    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.position.size(), 2u);

    float min_x = std::numeric_limits<float>::max();
    float max_x = -std::numeric_limits<float>::max();
    for (const auto& pos : system.particles.core.position) {
        min_x = std::min(min_x, pos.x);
        max_x = std::max(max_x, pos.x);
    }
    EXPECT_GT(max_x - min_x, 0.2f);

    system.emitters[0].world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    trigger_particle_emitter(system, 0, 1);
    tick_particle_system(system, 0.1f);

    ASSERT_EQ(system.particles.core.position.size(), 4u);

    float oldest_age = 0.0f;
    float newest_age = std::numeric_limits<float>::max();
    float oldest_x = 0.0f;
    float newest_x = 0.0f;
    for (size_t i = 0; i < system.particles.core.position.size(); ++i) {
        const float age = system.particles.core.age[i];
        const float x = system.particles.core.position[i].x;
        if (age > oldest_age) {
            oldest_age = age;
            oldest_x = x;
        }
        if (age < newest_age) {
            newest_age = age;
            newest_x = x;
        }
    }
    EXPECT_GT(oldest_age - newest_age, 0.05f);
    EXPECT_LT(oldest_x, newest_x);

    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].mode, ParticleRenderMode::velocity_aligned);
}

TEST(RenderParticles, TickTriggeredContinuousEmitterDoesNotLatchIntoContinuousEmission)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.blend = ParticleBlendMode::additive;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::velocity_aligned;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.trigger_on_effect_events = true;
    emitter.emission.effect_event_period = 0.1f;
    emitter.emission.rate = 20.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 16;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    trigger_particle_emitter(system, 0, 1);
    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.position.size(), 2u);

    tick_particle_system(system, 0.1f);
    EXPECT_EQ(system.particles.core.position.size(), 2u);
}

TEST(RenderParticles, TickSpawnAttachedEmitterLeavesTrailWhenEmitterMoves)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 20.0f;
    emitter.simulation_space = ParticleSimulationSpace::spawn_attached;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 64;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].active = true;

    for (int i = 0; i < 10; ++i) {
        system.emitters[0].world_transform[3] = glm::vec4(static_cast<float>(i + 1), 0.0f, 0.0f, 1.0f);
        tick_particle_system(system, 0.1f);
    }

    ASSERT_FALSE(system.particles.core.position.empty());
    float min_x = system.particles.core.position.front().x;
    float max_x = min_x;
    for (const auto& position : system.particles.core.position) {
        min_x = std::min(min_x, position.x);
        max_x = std::max(max_x, position.x);
    }

    EXPECT_LT(min_x, max_x);
    EXPECT_GT(max_x - min_x, 1.0f);
}

TEST(RenderParticles, TickSpawnAttachedZeroSpeedParticlesCaptureEmitterTravelDirection)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::velocity_aligned;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 20.0f;
    emitter.simulation_space = ParticleSimulationSpace::spawn_attached;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 64;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    for (int i = 0; i < 4; ++i) {
        system.emitters[0].world_transform[3] = glm::vec4(static_cast<float>(i + 1), 0.0f, 0.0f, 1.0f);
        tick_particle_system(system, 0.1f);
    }

    ASSERT_FALSE(system.particles.core.render_direction.empty());
    for (const auto& direction : system.particles.core.render_direction) {
        EXPECT_NEAR(direction.x, 0.0f, 1.0e-6f);
        EXPECT_NEAR(direction.y, 0.0f, 1.0e-6f);
        EXPECT_NEAR(direction.z, 0.0f, 1.0e-6f);
    }
}

TEST(RenderParticles, TickVelocityAlignedZeroSpeedParticlesKeepEmitDirectionFromSpread)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::velocity_aligned;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::spawn_attached;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.initial.spread_radians = 6.28318530718f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);

    ASSERT_FALSE(system.particles.core.render_direction.empty());
    bool found_nonzero_direction = false;
    for (const auto& direction : system.particles.core.render_direction) {
        if (glm::dot(direction, direction) > 1.0e-6f) {
            found_nonzero_direction = true;
            break;
        }
    }
    EXPECT_TRUE(found_nonzero_direction);
}

TEST(RenderParticles, BillboardLocalZUsesEmitterLocalPlane)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard_local_z;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].world_transform = glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});

    tick_particle_system(system, 0.1f);
    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].mode, ParticleRenderMode::billboard_local_z);

    const auto indices = particle_render_packet_indices(system, packets[0]);
    ASSERT_FALSE(indices.empty());

    ParticleBillboardView view{.camera_position = glm::vec3{0.0f, 10.0f, 0.0f}};
    const ParticleBillboardAxes axes = resolve_particle_billboard_axes(view, system, packets[0], indices.front(),
        ParticleBillboardAxes{.right = glm::vec3{-1.0f, 0.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
    const glm::vec3 normal = glm::normalize(glm::cross(axes.right, axes.up));

    EXPECT_NEAR(std::abs(glm::dot(axes.right, glm::vec3{0.0f, 0.0f, -1.0f})), 1.0f, 1.0e-5f);
    EXPECT_NEAR(std::abs(glm::dot(axes.up, glm::vec3{0.0f, 1.0f, 0.0f})), 1.0f, 1.0e-5f);
    EXPECT_NEAR(std::abs(glm::dot(normal, glm::vec3{1.0f, 0.0f, 0.0f})), 1.0f, 1.0e-5f);

    view.camera_position = glm::vec3{0.0f, -10.0f, 5.0f};
    const ParticleBillboardAxes moved_camera_axes = resolve_particle_billboard_axes(view, system, packets[0],
        indices.front(), ParticleBillboardAxes{.right = glm::vec3{-1.0f, 0.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
    const glm::vec3 moved_camera_normal = glm::normalize(glm::cross(moved_camera_axes.right, moved_camera_axes.up));
    EXPECT_NEAR(std::abs(glm::dot(moved_camera_normal, normal)), 1.0f, 1.0e-5f);
}

TEST(RenderParticles, BillboardWorldZUsesStableWorldPlane)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard_world_z;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);
    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].mode, ParticleRenderMode::billboard_world_z);

    const auto indices = particle_render_packet_indices(system, packets[0]);
    ASSERT_FALSE(indices.empty());

    ParticleBillboardView view{.camera_position = glm::vec3{4.0f, 8.0f, 3.0f}};
    const ParticleBillboardAxes axes = resolve_particle_billboard_axes(view, system, packets[0], indices.front(),
        ParticleBillboardAxes{.right = glm::vec3{-1.0f, 0.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
    const glm::vec3 normal = glm::normalize(glm::cross(axes.right, axes.up));

    EXPECT_NEAR(glm::dot(axes.right, glm::vec3{1.0f, 0.0f, 0.0f}), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(axes.up, glm::vec3{0.0f, 1.0f, 0.0f}), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(normal, glm::vec3{0.0f, 0.0f, 1.0f}), 1.0f, 1.0e-5f);

    view.camera_position = glm::vec3{-8.0f, -4.0f, 6.0f};
    const ParticleBillboardAxes moved_camera_axes = resolve_particle_billboard_axes(view, system, packets[0],
        indices.front(), ParticleBillboardAxes{.right = glm::vec3{-1.0f, 0.0f, 0.0f}, .up = glm::vec3{0.0f, 0.0f, 1.0f}});
    EXPECT_NEAR(glm::dot(moved_camera_axes.right, axes.right), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(moved_camera_axes.up, axes.up), 1.0f, 1.0e-5f);
}

TEST(RenderParticles, AlignedWorldZSpriteRotationKeepsWorldUpAxis)
{
    const ParticleBillboardAxes axes{
        .right = glm::vec3{1.0f, 0.0f, 0.0f},
        .up = glm::vec3{0.0f, 0.0f, 1.0f},
    };

    const ParticleBillboardAxes aligned = particle_billboard_sprite_axes(
        ParticleRenderMode::aligned_world_z, axes, 0.25f);
    EXPECT_NEAR(glm::dot(aligned.right, glm::vec3{0.0f, 1.0f, 0.0f}), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(aligned.up, axes.up), 1.0f, 1.0e-5f);

    const ParticleBillboardAxes camera_facing = particle_billboard_sprite_axes(
        ParticleRenderMode::billboard, axes, 0.25f);
    EXPECT_NEAR(glm::dot(camera_facing.right, axes.up), 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::dot(camera_facing.up, -axes.right), 1.0f, 1.0e-5f);
}

TEST(RenderParticles, TickBillboardLocalZRotationDoesNotMoveAttachedLocalOffset)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard_local_z;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.rotation_rate = {1.0f, 1.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    ASSERT_TRUE(system.particles.has_attachment);

    system.emitters[0].active = false;
    system.particles.core.age[0] = 0.5f;
    system.particles.attachment.local_position[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    system.particles.core.position[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    system.particles.core.velocity[0] = glm::vec3{0.0f};

    tick_particle_system(system, 0.25f);

    const glm::vec3 local_position = system.particles.attachment.local_position[0];
    EXPECT_NEAR(local_position.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.z, 0.0f, 1.0e-5f);
    EXPECT_NEAR(glm::length(glm::vec2{local_position.x, local_position.y}), 1.0f, 1.0e-5f);
    EXPECT_NEAR(system.particles.core.rotation[0], 0.75f, 1.0e-5f);
}

TEST(RenderParticles, TickBillboardLocalZRectEmitterPreservesAuthoredCenterOffset)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard_local_z;
    emitter.region.type = ParticleSpawnRegionType::rect;
    emitter.region.size = {4.0f, 4.0f};
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.rotation_rate = {5.0f, 5.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    ASSERT_TRUE(system.particles.has_attachment);

    system.emitters[0].active = false;
    system.particles.core.age[0] = 0.25f;
    system.particles.core.normalized_age[0] = 0.125f;
    system.particles.attachment.local_position[0] = glm::vec3{0.02f, 0.02f, 0.125f};
    system.particles.core.position[0] = glm::vec3{0.02f, 0.02f, 0.125f};
    system.particles.core.velocity[0] = glm::vec3{0.0f};

    tick_particle_system(system, 0.25f);

    const glm::vec3 local_position = system.particles.attachment.local_position[0];
    EXPECT_NEAR(local_position.x, 0.02f, 1.0e-5f);
    EXPECT_NEAR(local_position.y, 0.02f, 1.0e-5f);
    EXPECT_NEAR(local_position.z, 0.125f, 1.0e-5f);
    EXPECT_NEAR(system.particles.core.rotation[0], 2.5f, 1.0e-5f);
}

TEST(RenderParticles, TickBillboardLocalZPreservesLocalNormalMotion)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard_local_z;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    ASSERT_TRUE(system.particles.has_attachment);

    system.emitters[0].active = false;
    system.particles.attachment.local_position[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    system.particles.core.position[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    system.particles.core.velocity[0] = glm::vec3{0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.25f);

    const glm::vec3 local_position = system.particles.attachment.local_position[0];
    EXPECT_NEAR(local_position.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.z, 0.25f, 1.0e-5f);
}

TEST(RenderParticles, TickBillboardLocalZBlurLengthDoesNotScaleFootprint)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard_local_z;
    emitter.render.blur_length = 10.0f;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    ASSERT_TRUE(system.particles.has_attachment);

    system.emitters[0].active = false;
    system.particles.attachment.local_position[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    system.particles.core.position[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    system.particles.core.velocity[0] = glm::vec3{0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.25f);

    const glm::vec3 local_position = system.particles.attachment.local_position[0];
    EXPECT_NEAR(local_position.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.z, 0.25f, 1.0e-5f);
}

TEST(RenderParticles, TickBillboardLocalZZeroRadiusMotionStaysOnNormalAxis)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard_local_z;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 10.0f;
    emitter.simulation_space = ParticleSimulationSpace::emitter_attached;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.initial.speed = {0.0f, 0.0f};
    emitter.max_particles = 4;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.1f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    ASSERT_TRUE(system.particles.has_attachment);

    system.emitters[0].active = false;
    system.particles.attachment.local_position[0] = glm::vec3{0.0f};
    system.particles.core.position[0] = glm::vec3{0.0f};
    system.particles.core.velocity[0] = glm::vec3{0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.25f);

    const glm::vec3 local_position = system.particles.attachment.local_position[0];
    EXPECT_NEAR(local_position.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(local_position.z, 0.25f, 1.0e-5f);
}

TEST(RenderParticles, TickFollowsBezierTargeting)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.targeting.mode = ParticleTargetingMode::point_bezier;
    emitter.targeting.transition_factor = 1.0f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    ASSERT_EQ(compiled.emitters[0].kernel, CompiledParticleKernel::sprite_target_bezier);

    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.5f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].z, 0.5f);
}

TEST(RenderParticles, TickMaintainsBeamParticle)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::beam_continuous;
    emitter.initial.lifetime = {0.1f, 0.1f};
    emitter.targeting.mode = ParticleTargetingMode::beam_lightning;
    emitter.beam.update_interval = 0.1f;
    emitter.beam.jitter_radius = 0.25f;
    emitter.beam.noise_scale = 0.5f;
    emitter.beam.subdivisions = 6;
    emitter.max_particles = 1;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    ASSERT_EQ(compiled.emitters[0].kernel, CompiledParticleKernel::beam_lightning);

    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 2.0f};

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
    EXPECT_FLOAT_EQ(system.particles.core.position[0].z, 1.0f);
    EXPECT_GT(system.particles.core.lifetime[0], 1.0e8f);
    EXPECT_FLOAT_EQ(system.particles.beams.source_position[0].z, 0.0f);
    EXPECT_FLOAT_EQ(system.particles.beams.target_position[0].z, 2.0f);
    EXPECT_EQ(system.particles.beams.subdivisions[0], 6u);
    EXPECT_FLOAT_EQ(system.particles.beams.noise_scale[0], 0.5f);

    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].path.kind, ParticleRenderPathKind::beam);
    EXPECT_FLOAT_EQ(packets[0].path.source.z, 0.0f);
    EXPECT_FLOAT_EQ(packets[0].path.target.z, 2.0f);
    EXPECT_EQ(packets[0].path.subdivisions, 6u);

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.position.size(), 1);
}

TEST(RenderParticles, TickBeamLightningAdvancesAgeForSpriteSheets)
{
    ParticleEffectDef effect;
    ParticleMaterialDef material;
    material.sheet.columns = 2;
    material.sheet.rows = 2;
    material.sheet.frame_begin = 0;
    material.sheet.frame_end = 3;
    material.sheet.frames_per_second = 4.0f;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::beam_continuous;
    emitter.initial.lifetime = {0.1f, 0.1f};
    emitter.targeting.mode = ParticleTargetingMode::beam_lightning;
    emitter.max_particles = 1;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    ASSERT_EQ(compiled.emitters[0].kernel, CompiledParticleKernel::beam_lightning);
    EXPECT_NE(compiled.emitters[0].features & CompiledParticleFeature::update_frame, 0u);

    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 2.0f};

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    EXPECT_FLOAT_EQ(system.particles.core.age[0], 0.25f);
    EXPECT_EQ(system.particles.core.frame[0], 1u);

    tick_particle_system(system, 0.25f);
    ASSERT_EQ(system.particles.core.age.size(), 1u);
    EXPECT_FLOAT_EQ(system.particles.core.age[0], 0.5f);
    EXPECT_EQ(system.particles.core.frame[0], 2u);
}

TEST(RenderParticles, BuildRenderPacketsPreservesLinkedChainSequence)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::linked_chain;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 4.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);

    tick_particle_system(system, 0.75f);
    ASSERT_EQ(system.particles.core.position.size(), 3u);

    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].mode, ParticleRenderMode::linked_chain);
    EXPECT_TRUE(packets[0].preserve_sequence);
    EXPECT_FALSE(packets[0].sort_back_to_front);

    const auto begin = packets[0].begin;
    const auto first = system.sort_order[begin + 0];
    const auto second = system.sort_order[begin + 1];
    const auto third = system.sort_order[begin + 2];
    EXPECT_GE(system.particles.core.age[first], system.particles.core.age[second]);
    EXPECT_GE(system.particles.core.age[second], system.particles.core.age[third]);
}

TEST(RenderParticles, TickLinkedChainBezierKinkTraversesSpawnToTarget)
{
    // For linked_chain + point_bezier, each particle's kink position traverses
    // from its spawn position toward the target at rate age/combinetime. The
    // spawn position carries the lateral offset from the emitter's xsize/ysize
    // region — that off-axis displacement is what creates the visible V-kink shape.
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::linked_chain;
    emitter.emission.mode = ParticleEmissionMode::event_burst;
    emitter.emission.rate = 1.0f;
    emitter.initial.lifetime = {2.0f, 2.0f};
    emitter.max_particles = 4;
    emitter.targeting.mode = ParticleTargetingMode::point_bezier;
    emitter.targeting.transition_factor = 2.0f;
    emitter.targeting.source_tangent = 0.0f;
    emitter.targeting.target_tangent = 0.0f;
    emitter.region.type = ParticleSpawnRegionType::point;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 3.0f};

    trigger_particle_emitter(system, 0, 1);
    // Tick to age = 1.0s → path_t = 1.0/2.0 = 0.5 → kink midway between emitter and target
    tick_particle_system(system, 1.0f);

    const auto& core = system.particles.core;
    ASSERT_GT(core.age.size(), 0u);
    const auto& bezier = system.particles.bezier;
    for (size_t i = 0; i < core.age.size(); ++i) {
        const float path_t = std::clamp(core.age[i] / 2.0f, 0.0f, 1.0f);
        const glm::vec3 expected = glm::mix(bezier.source_position[i], bezier.target_position[i], path_t);
        EXPECT_NEAR(core.position[i].x, expected.x, 1.0e-4f);
        EXPECT_NEAR(core.position[i].y, expected.y, 1.0e-4f);
        EXPECT_NEAR(core.position[i].z, expected.z, 1.0e-4f);
    }
}

TEST(RenderParticles, BuildRenderPacketsDoNotExposeSharedBezierPathPayload)
{
    ParticleEffectDef effect;
    effect.materials.push_back(ParticleMaterialDef{});

    ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 2.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.targeting.mode = ParticleTargetingMode::point_bezier;
    emitter.targeting.transition_factor = 1.0f;
    emitter.targeting.source_tangent = 0.25f;
    emitter.targeting.target_tangent = 0.5f;
    emitter.max_particles = 8;
    effect.emitters.push_back(emitter);

    auto compiled = compile_particle_effect(effect).effect;
    auto system = create_particle_system(compiled);
    system.emitters[0].target_point = {0.0f, 0.0f, 1.0f};

    tick_particle_system(system, 0.5f);
    const auto packets = build_particle_render_packets(system);
    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].mode, ParticleRenderMode::billboard);
    EXPECT_EQ(packets[0].path.kind, ParticleRenderPathKind::none);
}

TEST(RenderParticles, ParticleEffectJsonRoundTrips)
{
    ParticleEffectDef effect;
    effect.name = "json_test";

    ParticleMaterialDef material;
    material.name = "smoke";
    material.texture = "fxpa_smoke01";
    material.blend = ParticleBlendMode::alpha;
    material.sheet.columns = 4;
    material.sheet.rows = 4;
    effect.materials.push_back(material);

    ParticleEmitterDef emitter;
    emitter.name = "smoke_emitter";
    emitter.render.material = 0;
    emitter.render.mode = ParticleRenderMode::billboard;
    emitter.emission.mode = ParticleEmissionMode::continuous;
    emitter.emission.metric = ParticleSpawnMetric::per_second;
    emitter.emission.rate = 12.0f;
    emitter.region.type = ParticleSpawnRegionType::rect;
    emitter.region.size = {100.0f, 20.0f};
    emitter.simulation_space = ParticleSimulationSpace::spawn_attached;
    emitter.initial.lifetime = {1.0f, 2.0f};
    emitter.initial.speed = {0.1f, 0.4f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    emitter.initial.color = {1.0f, 1.0f, 1.0f, 0.5f};
    emitter.over_life.alpha.keys = {{0.0f, 0.5f}, {1.0f, 0.0f}};
    emitter.max_particles = 64;
    effect.emitters.push_back(emitter);

    nlohmann::json j = effect;
    const auto restored = j.get<ParticleEffectDef>();

    ASSERT_EQ(restored.materials.size(), 1u);
    ASSERT_EQ(restored.emitters.size(), 1u);
    EXPECT_EQ(restored.name, "json_test");
    EXPECT_EQ(restored.materials[0].texture, "fxpa_smoke01");
    EXPECT_EQ(restored.emitters[0].name, "smoke_emitter");
    EXPECT_EQ(restored.emitters[0].simulation_space, ParticleSimulationSpace::spawn_attached);
    EXPECT_FLOAT_EQ(restored.emitters[0].region.size.x, 100.0f);
    EXPECT_FLOAT_EQ(restored.emitters[0].initial.color.w, 0.5f);
    ASSERT_EQ(restored.emitters[0].over_life.alpha.keys.size(), 2u);
    EXPECT_FLOAT_EQ(restored.emitters[0].over_life.alpha.keys.back().value, 0.0f);
}

TEST(RenderParticles, ParticleEffectJsonFileParses)
{
    std::ifstream input{"../tests/test_data/user/development/particle_basic.json"};
    ASSERT_TRUE(input.is_open());

    ParticleEffectDef effect;
    std::string error;
    ASSERT_TRUE(try_load_particle_effect_json(input, effect, &error)) << error;
    ASSERT_EQ(effect.name, "particle_basic");
    ASSERT_EQ(effect.materials.size(), 1u);
    ASSERT_EQ(effect.emitters.size(), 1u);

    const auto& material = effect.materials[0];
    EXPECT_EQ(material.name, "soft_smoke");
    EXPECT_EQ(material.texture, "fxpa_smoke01");
    EXPECT_EQ(material.blend, ParticleBlendMode::alpha);

    const auto& emitter = effect.emitters[0];
    EXPECT_EQ(emitter.name, "puff");
    EXPECT_EQ(emitter.emission.mode, ParticleEmissionMode::continuous);
    EXPECT_EQ(emitter.emission.metric, ParticleSpawnMetric::per_second);
    EXPECT_FLOAT_EQ(emitter.emission.rate, 6.0f);
    EXPECT_EQ(emitter.region.type, ParticleSpawnRegionType::rect);
    EXPECT_FLOAT_EQ(emitter.region.size.x, 25.0f);
    EXPECT_FLOAT_EQ(emitter.region.size.y, 25.0f);
    EXPECT_EQ(emitter.render.mode, ParticleRenderMode::billboard);
    EXPECT_EQ(emitter.render.material, 0u);
}

TEST(RenderParticles, ParticleEffectJsonTryLoadRejectsMissingTypeTag)
{
    std::istringstream input{R"({
        "name": "particle_missing_type",
        "materials": [],
        "emitters": []
    })"};

    ParticleEffectDef effect;
    std::string error;
    EXPECT_FALSE(try_load_particle_effect_json(input, effect, &error));
    EXPECT_NE(error.find("missing required \"$type\""), std::string::npos);
}

TEST(RenderParticles, ParticleEffectJsonTryLoadReportsNestedFieldErrors)
{
    std::istringstream input{R"({
        "$type": "ParticleEffect",
        "name": "particle_bad_range",
        "materials": [],
        "emitters": [{
            "initial": {
                "lifetime": {
                    "max": 1.0
                }
            }
        }]
    })"};

    ParticleEffectDef effect;
    std::string error;
    EXPECT_FALSE(try_load_particle_effect_json(input, effect, &error));
    EXPECT_NE(error.find("min"), std::string::npos);
}

} // namespace
