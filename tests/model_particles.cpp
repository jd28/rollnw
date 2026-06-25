#include <gtest/gtest.h>

#include <nw/model/Mdl.hpp>
#include <nw/model/mdl_particle_import.hpp>
#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_system.hpp>

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <sstream>

namespace {

TEST(ModelParticles, ImportTorchSmokeEmitter)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/it_torch_000.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    EXPECT_FALSE(result.effect.emitters.empty());
    EXPECT_FALSE(result.effect.materials.empty());

    const auto* smoke = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& emitter : result.effect.emitters) {
            if (emitter.name == "emmiter_smoke") return &emitter;
        }
        return nullptr;
    }();

    ASSERT_NE(smoke, nullptr);
    EXPECT_EQ(smoke->emission.mode, nw::render::ParticleEmissionMode::continuous);
    EXPECT_EQ(smoke->emission.metric, nw::render::ParticleSpawnMetric::per_second);
    EXPECT_EQ(smoke->render.mode, nw::render::ParticleRenderMode::billboard);
    EXPECT_EQ(smoke->simulation_space, nw::render::ParticleSimulationSpace::world);
    EXPECT_FLOAT_EQ(smoke->initial.mass, -0.2f);
    EXPECT_FLOAT_EQ(smoke->initial.speed.min, 0.0f);
    EXPECT_FLOAT_EQ(smoke->initial.speed.max, 0.2f);
    EXPECT_FLOAT_EQ(smoke->initial.size_y.min, smoke->initial.size_x.min);
    EXPECT_FLOAT_EQ(smoke->over_life.size_y.keys.back().value, smoke->over_life.size_x.keys.back().value);
    EXPECT_FLOAT_EQ(smoke->over_life.size_x.keys.back().value, 2.0f);
    EXPECT_GE(smoke->max_particles, 24u);
    ASSERT_LT(smoke->render.material, result.effect.materials.size());
    EXPECT_TRUE(result.effect.materials[smoke->render.material].sheet.random_start);
}

TEST(ModelParticles, ImportExplosionEmitter)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vwp_flash_whiblu.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    ASSERT_EQ(result.effect.emitters.size(), 1);

    const auto& emitter = result.effect.emitters[0];
    EXPECT_EQ(emitter.emission.mode, nw::render::ParticleEmissionMode::event_burst);
    EXPECT_EQ(emitter.render.mode, nw::render::ParticleRenderMode::stretched);
    EXPECT_EQ(result.effect.materials[emitter.render.material].blend, nw::render::ParticleBlendMode::additive);
    EXPECT_GE(emitter.max_particles, 1u);
}

TEST(ModelParticles, ImportDetonateAnimationEvents)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_detonate_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl, "impact");
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    ASSERT_EQ(result.effect_events.size(), 2u);
    EXPECT_FLOAT_EQ(result.effect_events[0].time, 0.0f);
    EXPECT_EQ(result.effect_events[0].burst_count, 1u);
    EXPECT_FLOAT_EQ(result.effect_events[1].time, 0.5f);
    EXPECT_EQ(result.effect_events[1].burst_count, 2u);
}

TEST(ModelParticles, ApplyImportEventsTriggersBurstEmitters)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_detonate_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto import = nw::model::import_particle_effect(mdl, "impact");
    auto compiled = nw::render::compile_particle_effect(import.effect).effect;
    auto system = nw::render::create_particle_system(compiled);
    nw::model::apply_particle_import_inits(import, system);

    nw::model::apply_particle_import_events(import, system, 0.0f, 0.25f, 1.0f, true);
    nw::render::tick_particle_system(system, 0.016f);
    ASSERT_EQ(system.particles.core.age.size(), 10u);

    nw::model::apply_particle_import_events(import, system, 0.25f, 0.75f, 1.0f, false);
    nw::render::tick_particle_system(system, 0.016f);
    ASSERT_EQ(system.particles.core.age.size(), 30u);

    nw::model::apply_particle_import_events(import, system, 0.75f, 0.1f, 1.0f, false);
    nw::render::tick_particle_system(system, 0.016f);
    ASSERT_EQ(system.particles.core.age.size(), 40u);
}

TEST(ModelParticles, ImportDetonateAnimationCanPulseContinuousEmitters)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_event_pulse_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl, "travel01");
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    ASSERT_EQ(result.effect_events.size(), 3u);
    const auto& emitter = result.effect.emitters[0];
    EXPECT_EQ(emitter.emission.mode, nw::render::ParticleEmissionMode::continuous);
    EXPECT_TRUE(emitter.emission.trigger_on_effect_events);
    EXPECT_FLOAT_EQ(emitter.emission.effect_event_period, 0.1f);
    EXPECT_EQ(emitter.render.mode, nw::render::ParticleRenderMode::velocity_aligned);
    EXPECT_EQ(emitter.simulation_space, nw::render::ParticleSimulationSpace::emitter_attached);
    EXPECT_FLOAT_EQ(emitter.initial.size_y.min, emitter.initial.size_x.min);
    EXPECT_FLOAT_EQ(emitter.over_life.size_y.keys.back().value, emitter.over_life.size_x.keys.back().value);
}

TEST(ModelParticles, ApplyImportEventsTriggersPulsedContinuousEmitters)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_event_pulse_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto import = nw::model::import_particle_effect(mdl, "travel01");
    auto compiled = nw::render::compile_particle_effect(import.effect).effect;
    auto baseline = nw::render::create_particle_system(compiled);
    nw::model::apply_particle_import_inits(import, baseline);
    nw::render::tick_particle_system(baseline, 0.1f);
    ASSERT_EQ(baseline.particles.core.age.size(), 0u);

    auto system = nw::render::create_particle_system(compiled);
    nw::model::apply_particle_import_inits(import, system);
    nw::model::apply_particle_import_events(import, system, 0.0f, 0.05f, 0.4f, true);
    nw::render::tick_particle_system(system, 0.1f);

    ASSERT_GT(system.particles.core.age.size(), baseline.particles.core.age.size());
}

TEST(ModelParticles, ImportBirthrateKeyCurve)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/it_torch_000.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* smoke = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& emitter : result.effect.emitters) {
            if (emitter.name == "emmiter_smoke") return &emitter;
        }
        return nullptr;
    }();

    ASSERT_NE(smoke, nullptr);
    ASSERT_FALSE(smoke->emission.rate_over_time.keys.empty());
    EXPECT_FLOAT_EQ(smoke->emission.rate_over_time.keys.front().time, 0.0f);
    EXPECT_FLOAT_EQ(smoke->emission.rate_over_time.keys.front().value, 0.0f);
}

TEST(ModelParticles, ImportSpawnAnimatedCurves)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/plc_cndl02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : result.effect.emitters) {
            if (!it.spawn_over_time.alpha_start.keys.empty() || !it.spawn_over_time.size_start_x.keys.empty()) return &it;
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_FALSE(emitter->spawn_over_time.alpha_start.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.size_start_x.keys.empty());
}

TEST(ModelParticles, ImportStartEndOnlyCurvesUseLinearMidpointFallbacks)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_start_end_only_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    ASSERT_EQ(result.effect.emitters.size(), 1u);

    const auto& emitter = result.effect.emitters[0];
    ASSERT_EQ(emitter.over_life.color.keys.size(), 3u);
    ASSERT_EQ(emitter.over_life.alpha.keys.size(), 3u);
    ASSERT_EQ(emitter.over_life.size_x.keys.size(), 3u);

    EXPECT_FLOAT_EQ(emitter.over_life.color.keys[1].time, 0.5f);
    EXPECT_FLOAT_EQ(emitter.over_life.color.keys[1].value.r, 0.5f);
    EXPECT_FLOAT_EQ(emitter.over_life.color.keys[1].value.g, 0.5f);
    EXPECT_FLOAT_EQ(emitter.over_life.color.keys[1].value.b, 0.5f);
    EXPECT_FLOAT_EQ(emitter.over_life.alpha.keys[1].time, 0.5f);
    EXPECT_FLOAT_EQ(emitter.over_life.alpha.keys[1].value, 0.5f);
    EXPECT_FLOAT_EQ(emitter.over_life.size_x.keys[1].value, 2.0f);
}

TEST(ModelParticles, ImportWithoutAnimationKeepsStaticContinuousEmitterDefaults)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/plc_cndl02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl, {}, false);
    const auto* emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : result.effect.emitters) {
            if (it.name == "Candleflame") return &it;
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_EQ(emitter->emission.mode, nw::render::ParticleEmissionMode::continuous);
    EXPECT_FLOAT_EQ(emitter->emission.rate, 3.0f);
    EXPECT_TRUE(emitter->emission.rate_over_time.keys.empty());
}

TEST(ModelParticles, ImportChunkEmitterAsMesh)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/plc_cndl02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : result.effect.emitters) {
            if (it.name == "ChunkyWood") return &it;
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_EQ(emitter->render.mode, nw::render::ParticleRenderMode::mesh);
    ASSERT_LT(emitter->render.material, result.effect.materials.size());
    EXPECT_EQ(result.effect.materials[emitter->render.material].mesh, "plc_chunk_w01");
}

TEST(ModelParticles, ImportTintedEmitterFlag)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/plc_cndl02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : result.effect.emitters) {
            if (it.render.tint_to_scene_ambient) return &it;
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_TRUE(emitter->render.tint_to_scene_ambient);
}

TEST(ModelParticles, ImportAffectedByWindFlag)
{
    {
        std::ifstream in{"../tests/test_data/user/development/vfx_detonate_test.mdl"};
        ASSERT_TRUE(in.is_open());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        auto text = buffer.str();
        const auto pos = text.find("affectedByWind 0");
        ASSERT_NE(pos, std::string::npos);
        text.replace(pos, std::strlen("affectedByWind 0"), "affectedByWind 1");

        std::ofstream out{"./vfx_wind_generated_test.mdl"};
        ASSERT_TRUE(out.is_open());
        out << text;
    }

    nw::model::Mdl mdl{"./vfx_wind_generated_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    EXPECT_TRUE(result.effect.emitters[0].affected_by_wind);
}

TEST(ModelParticles, ImportBlastForceEvent)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/plc_palm02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* force_emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& emitter : result.effect.emitters) {
            if (emitter.force_event.radius > 0.0f || emitter.force_event.length > 0.0f) { return &emitter; }
        }
        return nullptr;
    }();

    ASSERT_NE(force_emitter, nullptr);
    EXPECT_FLOAT_EQ(force_emitter->force_event.radius, 0.2f);
    EXPECT_FLOAT_EQ(force_emitter->force_event.length, 1.0f);
}

TEST(ModelParticles, ImportCollisionBounce)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/plc_cndl02.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* collision_emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& emitter : result.effect.emitters) {
            if (emitter.collision.enabled) { return &emitter; }
        }
        return nullptr;
    }();

    ASSERT_NE(collision_emitter, nullptr);
    EXPECT_TRUE(collision_emitter->collision.bounce);
    EXPECT_FALSE(collision_emitter->collision.splat);
    EXPECT_FLOAT_EQ(collision_emitter->collision.bounce_coefficient, 0.5f);
}

TEST(ModelParticles, ImportSpawnAnimatedMotionCurves)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_spawn_motion_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl, "impact");
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    const auto* emitter = &result.effect.emitters.front();
    ASSERT_NE(emitter, nullptr);
    EXPECT_FALSE(emitter->spawn_over_time.lifetime.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.speed.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.speed_random.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.mass.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.rotation_rate.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.spread.keys.empty());
}

TEST(ModelParticles, ImportSpawnAnimatedSpriteSheetCurves)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/fx_step_stbrdg.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : result.effect.emitters) {
            if (!it.spawn_over_time.sheet_frame_begin.keys.empty()
                || !it.spawn_over_time.sheet_frame_end.keys.empty()
                || !it.spawn_over_time.sheet_fps.keys.empty()
                || !it.spawn_over_time.sheet_random_start.keys.empty()) {
                return &it;
            }
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_FALSE(emitter->spawn_over_time.sheet_frame_begin.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.sheet_frame_end.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.sheet_fps.keys.empty());
    EXPECT_FALSE(emitter->spawn_over_time.sheet_random_start.keys.empty());
}

TEST(ModelParticles, ImportNegativeBirthrateClampsParticleBudget)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/fx_step_stbrdg.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto import = nw::model::import_particle_effect(mdl, {}, false);
    ASSERT_EQ(import.effect.emitters.size(), 1u);

    const auto& emitter = import.effect.emitters.front();
    EXPECT_FLOAT_EQ(emitter.emission.rate, 0.0f);
    EXPECT_LE(emitter.max_particles, 2u);

    auto compiled = nw::render::compile_particle_effect(import.effect).effect;
    EXPECT_LE(compiled.max_particles_total, 2u);
    auto system = nw::render::create_particle_system(compiled);
    EXPECT_LE(system.particles.core.position.capacity(), 2u);
}

TEST(ModelParticles, ImportThreeStageOverLifeCurves)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_midcurve_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    const auto& emitter = result.effect.emitters[0];

    ASSERT_EQ(emitter.over_life.alpha.keys.size(), 3u);
    EXPECT_FLOAT_EQ(emitter.over_life.alpha.keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(emitter.over_life.alpha.keys[1].time, 0.25f);
    EXPECT_FLOAT_EQ(emitter.over_life.alpha.keys[2].time, 1.0f);
    EXPECT_FLOAT_EQ(emitter.over_life.alpha.keys[1].value, 0.25f);

    ASSERT_EQ(emitter.over_life.size_x.keys.size(), 3u);
    EXPECT_FLOAT_EQ(emitter.over_life.size_x.keys[1].time, 0.25f);
    EXPECT_FLOAT_EQ(emitter.over_life.size_x.keys[1].value, 2.0f);

    ASSERT_EQ(emitter.over_life.color.keys.size(), 3u);
    EXPECT_FLOAT_EQ(emitter.over_life.color.keys[1].time, 0.25f);
    EXPECT_FLOAT_EQ(emitter.over_life.color.keys[1].value.g, 1.0f);
    EXPECT_FLOAT_EQ(emitter.over_life.color.keys[2].value.b, 1.0f);
}

TEST(ModelParticles, ImportSpawnAttachedEmitter)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/tts01_h02_03.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : result.effect.emitters) {
            if (it.name == "OmenEmitter01") return &it;
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_EQ(emitter->simulation_space, nw::render::ParticleSimulationSpace::spawn_attached);
    EXPECT_EQ(emitter->render.mode, nw::render::ParticleRenderMode::billboard);
    EXPECT_FLOAT_EQ(emitter->initial.speed.min, 1.0f);
    EXPECT_FLOAT_EQ(emitter->initial.speed.max, 1.3f);
    ASSERT_LT(emitter->render.material, result.effect.materials.size());
    EXPECT_TRUE(result.effect.materials[emitter->render.material].sheet.random_start);
}

TEST(ModelParticles, ImportEmitterAttachedEmitter)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/tts01_h02_03.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto* emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : result.effect.emitters) {
            if (it.name == "OmenEmitter02") return &it;
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_EQ(emitter->simulation_space, nw::render::ParticleSimulationSpace::emitter_attached);
    EXPECT_EQ(emitter->emission.mode, nw::render::ParticleEmissionMode::continuous);
    EXPECT_EQ(emitter->render.mode, nw::render::ParticleRenderMode::billboard);
    EXPECT_FLOAT_EQ(emitter->initial.speed.min, 0.0f);
    EXPECT_FLOAT_EQ(emitter->initial.speed.max, 0.0f);
    ASSERT_LT(emitter->render.material, result.effect.materials.size());
    EXPECT_TRUE(result.effect.materials[emitter->render.material].sheet.random_start);
}

TEST(ModelParticles, ImportGravityTargetedEmitter)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/c_mindflayer.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto match = [&]() -> std::pair<const nw::render::ParticleEmitterDef*, const nw::model::ParticleImportEmitterInit*> {
        for (size_t i = 0; i < result.effect.emitters.size(); ++i) {
            const auto& it = result.effect.emitters[i];
            if (it.targeting.mode == nw::render::ParticleTargetingMode::point_gravity) {
                return {&it, &result.emitter_inits[i]};
            }
        }
        return {nullptr, nullptr};
    }();

    ASSERT_NE(match.first, nullptr);
    ASSERT_NE(match.second, nullptr);
    EXPECT_FLOAT_EQ(match.first->targeting.gravity, 3.0f);
    EXPECT_GT(match.first->targeting.drag, 0.0f);
    EXPECT_FALSE(match.second->emitter_node_name.empty());
    ASSERT_NE(match.second->emitter_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    ASSERT_LT(match.second->emitter_source_node_index, mdl.model.nodes.size());
    EXPECT_EQ(mdl.model.nodes[match.second->emitter_source_node_index]->name, match.second->emitter_node_name);
    EXPECT_FALSE(match.second->target_node_name.empty());
    ASSERT_NE(match.second->target_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    ASSERT_LT(match.second->target_source_node_index, mdl.model.nodes.size());
    EXPECT_EQ(mdl.model.nodes[match.second->target_source_node_index]->name, match.second->target_node_name);
    EXPECT_TRUE(match.second->has_default_target_offset);
    EXPECT_GT(match.second->default_target_offset.z, 0.0f);
}

TEST(ModelParticles, ImportAnimationSelectedBirthrateCurve)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/c_mindflayer.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto base = nw::model::import_particle_effect(mdl);
    auto selected = nw::model::import_particle_effect(mdl, "cspecial");

    const auto* base_emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : base.effect.emitters) {
            if (it.name == "OmenEmitter01") return &it;
        }
        return nullptr;
    }();
    const auto* selected_emitter = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& it : selected.effect.emitters) {
            if (it.name == "OmenEmitter01") return &it;
        }
        return nullptr;
    }();

    ASSERT_NE(base_emitter, nullptr);
    ASSERT_NE(selected_emitter, nullptr);
    ASSERT_FALSE(base_emitter->emission.rate_over_time.keys.empty());
    ASSERT_FALSE(selected_emitter->emission.rate_over_time.keys.empty());
    ASSERT_GT(selected_emitter->emission.rate_over_time.keys.size(), 1u);
    EXPECT_FLOAT_EQ(base_emitter->emission.rate_over_time.keys.front().value, 0.0f);
    EXPECT_GT(selected_emitter->emission.rate_over_time.keys[1].value, 0.0f);
    EXPECT_GT(selected_emitter->max_particles, base_emitter->max_particles);
}

TEST(ModelParticles, ImportBezierTargetedEmitter)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/T_Door21.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    const auto match = [&]() -> std::pair<const nw::render::ParticleEmitterDef*, const nw::model::ParticleImportEmitterInit*> {
        for (size_t i = 0; i < result.effect.emitters.size(); ++i) {
            const auto& it = result.effect.emitters[i];
            if (it.name == "ground_lightning01") return {&it, &result.emitter_inits[i]};
        }
        return {nullptr, nullptr};
    }();

    ASSERT_NE(match.first, nullptr);
    ASSERT_NE(match.second, nullptr);
    EXPECT_EQ(match.first->targeting.mode, nw::render::ParticleTargetingMode::point_bezier);
    EXPECT_EQ(match.first->simulation_space, nw::render::ParticleSimulationSpace::spawn_attached);
    EXPECT_EQ(match.first->render.mode, nw::render::ParticleRenderMode::linked_chain);
    EXPECT_FLOAT_EQ(match.first->targeting.transition_factor, 1.0f);
    ASSERT_NE(match.second->emitter_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    ASSERT_LT(match.second->emitter_source_node_index, mdl.model.nodes.size());
    EXPECT_EQ(mdl.model.nodes[match.second->emitter_source_node_index]->name, match.second->emitter_node_name);
    ASSERT_NE(match.second->target_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    ASSERT_LT(match.second->target_source_node_index, mdl.model.nodes.size());
    EXPECT_EQ(mdl.model.nodes[match.second->target_source_node_index]->name, match.second->target_node_name);
    EXPECT_TRUE(match.second->has_default_target_offset);
    EXPECT_LT(match.second->default_target_offset.z, 0.0f);
}

TEST(ModelParticles, ImportNonTargetedLinkedMistAsWorldBillboard)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_linked_mist_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    ASSERT_EQ(result.effect.emitters.size(), 1u);

    const auto& emitter = result.effect.emitters[0];
    ASSERT_LT(emitter.render.material, result.effect.materials.size());
    EXPECT_EQ(emitter.targeting.mode, nw::render::ParticleTargetingMode::none);
    EXPECT_EQ(emitter.render.mode, nw::render::ParticleRenderMode::billboard_world_z);
    EXPECT_EQ(result.effect.materials[emitter.render.material].blend, nw::render::ParticleBlendMode::additive);
}

TEST(ModelParticles, ImportStationaryNormalRectAuraAsLocalPlane)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_normal_rect_aura_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);

    const auto* aura = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& emitter : result.effect.emitters) {
            if (emitter.name == "portal_aura") return &emitter;
        }
        return nullptr;
    }();
    const auto* spark = [&]() -> const nw::render::ParticleEmitterDef* {
        for (const auto& emitter : result.effect.emitters) {
            if (emitter.name == "plain_spark") return &emitter;
        }
        return nullptr;
    }();

    ASSERT_NE(aura, nullptr);
    ASSERT_NE(spark, nullptr);
    EXPECT_EQ(aura->render.mode, nw::render::ParticleRenderMode::billboard_local_z);
    EXPECT_EQ(aura->region.type, nw::render::ParticleSpawnRegionType::rect);
    EXPECT_FLOAT_EQ(aura->region.size.x, 26.0f);
    EXPECT_FLOAT_EQ(aura->region.size.y, 41.0f);
    ASSERT_LT(aura->render.material, result.effect.materials.size());
    EXPECT_EQ(result.effect.materials[aura->render.material].blend, nw::render::ParticleBlendMode::additive);
    EXPECT_EQ(spark->render.mode, nw::render::ParticleRenderMode::billboard);
}

TEST(ModelParticles, ParseEmitterFlags)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/T_Door21.mdl"};
    ASSERT_TRUE(mdl.valid());

    const auto* emitter = [&]() -> const nw::model::EmitterNode* {
        for (const auto& node : mdl.model.nodes) {
            if (node && node->type == nw::model::NodeType::emitter && node->name == "ground_lightning01") {
                return static_cast<const nw::model::EmitterNode*>(node.get());
            }
        }
        return nullptr;
    }();

    ASSERT_NE(emitter, nullptr);
    EXPECT_NE(emitter->flags & nw::model::EmitterFlag::InheritLocal, 0u);
    EXPECT_NE(emitter->flags & nw::model::EmitterFlag::Random, 0u);
}

TEST(ModelParticles, ApplyImportInitsSetsDefaultTargetPoints)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/T_Door21.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto import = nw::model::import_particle_effect(mdl);
    auto compiled = nw::render::compile_particle_effect(import.effect).effect;
    auto system = nw::render::create_particle_system(compiled);

    size_t emitter_index = import.effect.emitters.size();
    glm::vec3 default_position{0.0f};
    glm::vec3 target{0.0f};
    for (size_t i = 0; i < import.effect.emitters.size(); ++i) {
        if (import.effect.emitters[i].name == "ground_lightning01") {
            emitter_index = i;
            default_position = import.emitter_inits[i].default_position;
            target = import.emitter_inits[i].default_target_offset;
            break;
        }
    }

    ASSERT_LT(emitter_index, system.emitters.size());
    nw::model::apply_particle_import_inits(import, system);
    EXPECT_EQ(glm::vec3(system.emitters[emitter_index].world_transform[3]), default_position);
    EXPECT_EQ(system.emitters[emitter_index].prev_world_pos, default_position);
    EXPECT_EQ(system.emitters[emitter_index].target_point, target);
}

TEST(ModelParticles, ImportLightningEmitter)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/vfx_lightning_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);
    ASSERT_EQ(result.effect.emitters.size(), 1u);
    ASSERT_EQ(result.emitter_inits.size(), 1u);

    const auto& emitter = result.effect.emitters[0];
    EXPECT_EQ(emitter.emission.mode, nw::render::ParticleEmissionMode::beam_continuous);
    EXPECT_EQ(emitter.targeting.mode, nw::render::ParticleTargetingMode::beam_lightning);
    EXPECT_EQ(emitter.render.mode, nw::render::ParticleRenderMode::beam);
    EXPECT_FLOAT_EQ(emitter.beam.update_interval, 0.1f);
    EXPECT_FLOAT_EQ(emitter.beam.jitter_radius, 0.25f);
    EXPECT_FLOAT_EQ(emitter.beam.noise_scale, 0.5f);
    EXPECT_EQ(emitter.beam.subdivisions, 6u);
    EXPECT_NE(result.emitter_inits[0].emitter_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    EXPECT_NE(result.emitter_inits[0].target_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    EXPECT_TRUE(result.emitter_inits[0].has_default_target_offset);
    EXPECT_FLOAT_EQ(result.emitter_inits[0].default_target_offset.z, 2.0f);
}

TEST(ModelParticles, ImportInheritPartProducesDeferredWarning)
{
    nw::model::Mdl mdl{"../tests/test_data/user/development/c_mindflayer.mdl"};
    ASSERT_TRUE(mdl.valid());

    auto result = nw::model::import_particle_effect(mdl);

    const auto it = std::find_if(result.warnings.begin(), result.warnings.end(), [](const auto& warning) {
        return warning.field == "inherit_part";
    });

    ASSERT_NE(it, result.warnings.end());
    EXPECT_EQ(it->message,
        "inherit_part is present but currently deferred; no proven neutral lowering has been implemented yet");
}

TEST(ModelParticles, CorpusLocalFixturesImport)
{
    std::ifstream in{"../tests/test_data/user/development/particle_corpus.json"};
    ASSERT_TRUE(in.is_open());

    nlohmann::json manifest;
    in >> manifest;
    ASSERT_TRUE(manifest.is_object());
    ASSERT_TRUE(manifest.contains("entries"));
    ASSERT_TRUE(manifest["entries"].is_array());

    for (const auto& entry : manifest["entries"]) {
        if (!entry.is_object()) { continue; }
        if (entry.value("source", std::string{}) != "local_fixture") { continue; }

        const auto id = entry.value("id", std::string{"<unknown>"});
        const auto input = entry.value("input", std::string{});
        ASSERT_FALSE(input.empty()) << id;

        nw::model::Mdl mdl{"../" + input};
        ASSERT_TRUE(mdl.valid()) << id << " -> " << input;

        const auto animation = entry.value("animation", std::string{});
        auto result = nw::model::import_particle_effect(mdl, animation);
        EXPECT_FALSE(result.effect.emitters.empty()) << id << " -> " << input;
    }
}

TEST(ModelParticles, CorpusLocalFixturesWarningsAreKnown)
{
    std::ifstream in{"../tests/test_data/user/development/particle_corpus.json"};
    ASSERT_TRUE(in.is_open());

    nlohmann::json manifest;
    in >> manifest;
    ASSERT_TRUE(manifest.is_object());
    ASSERT_TRUE(manifest.contains("entries"));
    ASSERT_TRUE(manifest["entries"].is_array());

    for (const auto& entry : manifest["entries"]) {
        if (!entry.is_object()) { continue; }
        if (entry.value("source", std::string{}) != "local_fixture") { continue; }

        const auto id = entry.value("id", std::string{"<unknown>"});
        const auto input = entry.value("input", std::string{});
        ASSERT_FALSE(input.empty()) << id;

        nw::model::Mdl mdl{"../" + input};
        ASSERT_TRUE(mdl.valid()) << id << " -> " << input;

        const auto animation = entry.value("animation", std::string{});
        auto result = nw::model::import_particle_effect(mdl, animation);

        for (const auto& warning : result.warnings) {
            EXPECT_EQ(warning.field, "inherit_part")
                << id << " -> " << input << " produced unexpected warning field '" << warning.field << "'";
            EXPECT_EQ(
                warning.message,
                "inherit_part is present but currently deferred; no proven neutral lowering has been implemented yet")
                << id << " -> " << input << " produced unexpected warning message";
        }
    }
}

TEST(ModelParticles, CorpusLocalFixturesCompileAndCreateSystems)
{
    std::ifstream in{"../tests/test_data/user/development/particle_corpus.json"};
    ASSERT_TRUE(in.is_open());

    nlohmann::json manifest;
    in >> manifest;
    ASSERT_TRUE(manifest.is_object());
    ASSERT_TRUE(manifest.contains("entries"));
    ASSERT_TRUE(manifest["entries"].is_array());

    for (const auto& entry : manifest["entries"]) {
        if (!entry.is_object()) { continue; }
        if (entry.value("source", std::string{}) != "local_fixture") { continue; }

        const auto id = entry.value("id", std::string{"<unknown>"});
        const auto input = entry.value("input", std::string{});
        ASSERT_FALSE(input.empty()) << id;

        nw::model::Mdl mdl{"../" + input};
        ASSERT_TRUE(mdl.valid()) << id << " -> " << input;

        const auto animation = entry.value("animation", std::string{});
        auto import = nw::model::import_particle_effect(mdl, animation);
        auto compiled = nw::render::compile_particle_effect(import.effect);

        EXPECT_FALSE(compiled.effect.emitters.empty()) << id << " -> " << input;

        auto system = nw::render::create_particle_system(compiled.effect);
        EXPECT_EQ(system.emitters.size(), compiled.effect.emitters.size()) << id << " -> " << input;
        EXPECT_GE(system.particles.core.position.capacity(),
            static_cast<size_t>(compiled.effect.max_particles_total))
            << id << " -> " << input;
    }
}

TEST(ModelParticles, CorpusLocalFixturesTickAndBuildPackets)
{
    std::ifstream in{"../tests/test_data/user/development/particle_corpus.json"};
    ASSERT_TRUE(in.is_open());

    nlohmann::json manifest;
    in >> manifest;
    ASSERT_TRUE(manifest.is_object());
    ASSERT_TRUE(manifest.contains("entries"));
    ASSERT_TRUE(manifest["entries"].is_array());

    for (const auto& entry : manifest["entries"]) {
        if (!entry.is_object()) { continue; }
        if (entry.value("source", std::string{}) != "local_fixture") { continue; }

        const auto id = entry.value("id", std::string{"<unknown>"});
        const auto input = entry.value("input", std::string{});
        ASSERT_FALSE(input.empty()) << id;

        nw::model::Mdl mdl{"../" + input};
        ASSERT_TRUE(mdl.valid()) << id << " -> " << input;

        const auto animation = entry.value("animation", std::string{});
        auto import = nw::model::import_particle_effect(mdl, animation);
        auto compiled = nw::render::compile_particle_effect(import.effect).effect;
        auto system = nw::render::create_particle_system(compiled);
        nw::model::apply_particle_import_inits(import, system);

        EXPECT_NO_THROW({
            nw::render::tick_particle_system(system, 0.016f);
            auto packets = nw::render::build_particle_render_packets(system);
            (void)packets;
        }) << id
           << " -> " << input;
    }
}

} // namespace
