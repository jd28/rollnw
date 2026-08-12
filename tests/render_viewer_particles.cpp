#include <gtest/gtest.h>

#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_system.hpp>
#include <nw/render/viewer/preview_model_animation.hpp>
#include <nw/render/viewer/preview_scene.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::unique_ptr<nw::render::RenderModel> make_render_model_particle_owner()
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = "render_model_owner";
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    model->nodes.push_back(nw::render::Node{
        .parent = -1,
        .world_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 4.0f, 5.0f}),
    });
    model->animations.push_back(nw::render::AnimationClip{
        .name = "impact",
        .duration = 1.0f,
    });

    nw::render::ParticleEffectDef effect;
    effect.materials.push_back(nw::render::ParticleMaterialDef{});

    nw::render::ParticleEmitterDef emitter;
    emitter.attachment.emitter_attachment_point = 0u;
    emitter.render.material = 0;
    emitter.max_particles = 8;
    emitter.emission.rate = 8.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    effect.emitters.push_back(std::move(emitter));

    model->particle_systems.push_back(nw::render::ModelAssetParticleSystem{
        .name = "render_model_fx",
        .animation_name = "impact",
        .effect = std::move(effect),
        .effect_events = {{.time = 0.0f, .burst_count = 2u}},
        .animation_length = 1.0f,
    });
    return model;
}

nw::render::ParticleEffectDef make_test_particle_effect(std::string name)
{
    nw::render::ParticleEffectDef effect;
    effect.name = std::move(name);
    effect.materials.push_back(nw::render::ParticleMaterialDef{});

    nw::render::ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 8;
    emitter.emission.rate = 8.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    effect.emitters.push_back(std::move(emitter));
    return effect;
}

std::unique_ptr<nw::render::RenderModel> make_render_model_particle_selection_owner()
{
    auto model = std::make_unique<nw::render::RenderModel>();
    model->name = "render_model_particle_selection";
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    model->animations.push_back(nw::render::AnimationClip{
        .name = "idle",
        .duration = 1.0f,
    });
    model->animations.push_back(nw::render::AnimationClip{
        .name = "impact",
        .duration = 1.0f,
    });
    model->particle_systems.push_back(nw::render::ModelAssetParticleSystem{
        .name = "base_fx",
        .effect = make_test_particle_effect("base_fx"),
    });
    model->particle_systems.push_back(nw::render::ModelAssetParticleSystem{
        .name = "idle_fx",
        .animation_name = "idle",
        .effect = make_test_particle_effect("idle_fx"),
        .animation_length = 1.0f,
    });
    model->particle_systems.push_back(nw::render::ModelAssetParticleSystem{
        .name = "impact_fx",
        .animation_name = "impact",
        .effect = make_test_particle_effect("impact_fx"),
        .effect_events = {{.time = 0.0f, .burst_count = 1u}},
        .animation_length = 1.0f,
    });
    return model;
}

std::vector<std::string> scene_particle_effect_names(const nw::render::viewer::PreviewScene& scene)
{
    std::vector<std::string> result;
    result.reserve(scene.particles.size());
    for (const auto& scene_particles : scene.particles) {
        result.push_back(scene_particles.import.effect.name);
    }
    return result;
}

bool has_scene_particle_effect(
    const nw::render::viewer::PreviewScene& scene,
    std::string_view effect_name)
{
    const auto names = scene_particle_effect_names(scene);
    return std::find(names.begin(), names.end(), effect_name) != names.end();
}

} // namespace

TEST(RenderViewerParticles, StaleOwnerCommonHandleHidesParticleSystem)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_render_model_particle_owner());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    ASSERT_EQ(scene.particles.size(), 1u);

    scene.model_instances.destroy(scene.static_model_instance_handles[0]);
    scene.update(1000);

    EXPECT_FALSE(scene.particles[0].owner_visible_last);
    EXPECT_TRUE(scene.particles[0].system.particles.core.position.empty());
}

TEST(RenderViewerParticles, TargetPointUpdateUsesOwnerHandle)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_render_model_particle_owner());
    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    ASSERT_EQ(scene.particles.size(), 1u);
    ASSERT_EQ(scene.particles[0].system.emitters.size(), 1u);

    scene.set_particle_target_point(scene.static_model_instance_handles[0], 0u, glm::vec3{1.0f, 2.0f, 3.0f});
    EXPECT_EQ(scene.particles[0].system.emitters[0].target_point, glm::vec3(1.0f, 2.0f, 3.0f));

    scene.set_particle_target_point(nw::render::ModelInstanceHandle{}, 0u, glm::vec3{4.0f, 5.0f, 6.0f});
    EXPECT_EQ(scene.particles[0].system.emitters[0].target_point, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(RenderViewerParticles, RenderModelParticleSystemsUseCommonOwnerHandle)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_render_model_particle_owner());

    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    ASSERT_EQ(scene.particles.size(), 1u);

    auto& scene_particles = scene.particles.front();
    EXPECT_EQ(scene_particles.owner_model_index, 0u);
    EXPECT_EQ(scene_particles.owner_instance_handle, scene.static_model_instance_handles[0]);
    EXPECT_EQ(scene_particles.import.effect.name, "render_model_fx");
    ASSERT_EQ(scene_particles.import.effect_events.size(), 1u);
    EXPECT_FLOAT_EQ(scene_particles.import.effect_events[0].time, 0.0f);
    EXPECT_EQ(scene_particles.import.effect_events[0].burst_count, 2u);
    ASSERT_EQ(scene_particles.system.emitters.size(), 1u);
    ASSERT_EQ(scene_particles.system.emitter_attachments.size(), 1u);

    const auto& binding = scene_particles.system.emitter_attachments.front();
    EXPECT_EQ(binding.owner_instance_handle, scene.static_model_instance_handles[0]);
    EXPECT_EQ(binding.owner_model_index, 0u);
    EXPECT_EQ(binding.emitter_attachment_point, 0u);
    EXPECT_NEAR(scene_particles.system.emitters[0].world_transform[3].x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(scene_particles.system.emitters[0].world_transform[3].y, 4.0f, 1.0e-5f);
    EXPECT_NEAR(scene_particles.system.emitters[0].world_transform[3].z, 5.0f, 1.0e-5f);

    scene.rebuild_particles();
    ASSERT_EQ(scene.particles.size(), 1u);
    auto& rebuilt_scene_particles = scene.particles.front();
    EXPECT_EQ(rebuilt_scene_particles.owner_model_index, 0u);
    EXPECT_EQ(rebuilt_scene_particles.owner_instance_handle, scene.static_model_instance_handles[0]);
    EXPECT_EQ(rebuilt_scene_particles.import.effect.name, "render_model_fx");
    ASSERT_EQ(rebuilt_scene_particles.system.emitters.size(), 1u);
    EXPECT_NEAR(rebuilt_scene_particles.system.emitters[0].world_transform[3].x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(rebuilt_scene_particles.system.emitters[0].world_transform[3].y, 4.0f, 1.0e-5f);
    EXPECT_NEAR(rebuilt_scene_particles.system.emitters[0].world_transform[3].z, 5.0f, 1.0e-5f);

    scene.model_instances.destroy(scene.static_model_instance_handles[0]);
    scene.update(1000);

    EXPECT_FALSE(rebuilt_scene_particles.owner_visible_last);
    EXPECT_TRUE(rebuilt_scene_particles.system.particles.core.position.empty());
}

TEST(RenderViewerParticles, RenderModelParticleSystemsFollowSelectedClip)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_render_model_particle_selection_owner());

    ASSERT_EQ(scene.static_model_instance_handles.size(), 1u);
    ASSERT_EQ(scene.particles.size(), 1u);
    EXPECT_FALSE(has_scene_particle_effect(scene, "base_fx"));
    EXPECT_TRUE(has_scene_particle_effect(scene, "idle_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "impact_fx"));

    ASSERT_TRUE(nw::render::viewer::set_render_model_animation_clip_by_name(scene, "impact", 0.0f));

    ASSERT_EQ(scene.particles.size(), 1u);
    EXPECT_FALSE(has_scene_particle_effect(scene, "base_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "idle_fx"));
    EXPECT_TRUE(has_scene_particle_effect(scene, "impact_fx"));
    ASSERT_EQ(scene.particles.front().import.effect_events.size(), 1u);
    EXPECT_FLOAT_EQ(scene.particles.front().import.effect_events[0].time, 0.0f);
    EXPECT_EQ(scene.particles.front().import.effect_events[0].burst_count, 1u);
}

TEST(RenderViewerParticles, RenderModelParticleSystemsFallBackToBaseForClipWithoutParticlePayload)
{
    auto model = make_render_model_particle_selection_owner();
    model->animations.push_back(nw::render::AnimationClip{
        .name = "walk",
        .duration = 1.0f,
    });

    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));
    ASSERT_TRUE(nw::render::viewer::set_render_model_animation_clip_by_name(scene, "walk", 0.0f));

    ASSERT_EQ(scene.particles.size(), 1u);
    EXPECT_TRUE(has_scene_particle_effect(scene, "base_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "idle_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "impact_fx"));
}

TEST(RenderViewerParticles, RenderModelParticleSystemsWithoutAnimationSelectorUseOnlyBase)
{
    auto model = make_render_model_particle_selection_owner();
    model->animations.clear();

    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));

    ASSERT_EQ(scene.particles.size(), 1u);
    EXPECT_TRUE(has_scene_particle_effect(scene, "base_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "idle_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "impact_fx"));
}

TEST(RenderViewerParticles, RenderModelParticleSystemsKeepCompiledEffectAfterStorageGrowth)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_render_model_particle_selection_owner());
    scene.add(make_render_model_particle_selection_owner());

    ASSERT_FALSE(scene.particles.empty());
    for (const auto& particles : scene.particles) {
        EXPECT_EQ(particles.system.effect, &particles.compiled.effect);
    }
}
