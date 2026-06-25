#include <gtest/gtest.h>

#include <nw/model/Mdl.hpp>
#include <nw/render/nwn/model_loader.hpp>
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

std::unique_ptr<nw::render::nwn::ModelInstance> make_particle_owner_model()
{
    auto model = std::make_unique<nw::render::nwn::ModelInstance>();
    model->bounds = nw::render::Bounds{
        .min = {0.0f, 0.0f, 0.0f},
        .max = {1.0f, 1.0f, 1.0f},
    };
    model->render_enabled = true;
    return model;
}

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

void add_owner_particle_system(nw::render::viewer::PreviewScene& scene, size_t model_index)
{
    nw::render::ParticleEffectDef effect;
    effect.materials.push_back(nw::render::ParticleMaterialDef{});

    nw::render::ParticleEmitterDef emitter;
    emitter.render.material = 0;
    emitter.max_particles = 8;
    emitter.emission.rate = 8.0f;
    emitter.initial.lifetime = {1.0f, 1.0f};
    emitter.initial.size_x = {1.0f, 1.0f};
    emitter.initial.size_y = {1.0f, 1.0f};
    effect.emitters.push_back(std::move(emitter));

    auto& scene_particles = scene.particles.emplace_back();
    scene_particles.owner = scene.models[model_index].get();
    scene_particles.owner_model_index = static_cast<uint32_t>(model_index);
    scene_particles.owner_instance_handle = scene.model_instance_handles[model_index];
    scene_particles.import.effect = std::move(effect);
    scene_particles.compiled = nw::render::compile_particle_effect(scene_particles.import.effect);
    scene_particles.system = nw::render::create_particle_system(scene_particles.compiled.effect);
    scene_particles.system.effect = &scene_particles.compiled.effect;
}

} // namespace

TEST(RenderViewerParticles, StaleOwnerCommonHandleHidesParticleSystem)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_particle_owner_model());
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);

    add_owner_particle_system(scene, 0u);
    ASSERT_EQ(scene.particles.size(), 1u);

    scene.model_instances.destroy(scene.model_instance_handles[0]);
    scene.update(1000);

    EXPECT_FALSE(scene.particles[0].owner_visible_last);
    EXPECT_TRUE(scene.particles[0].system.particles.core.position.empty());
}

TEST(RenderViewerParticles, TargetPointUpdateUsesOwnerHandle)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_particle_owner_model());
    ASSERT_EQ(scene.model_instance_handles.size(), 1u);

    add_owner_particle_system(scene, 0u);
    ASSERT_EQ(scene.particles.size(), 1u);
    ASSERT_EQ(scene.particles[0].system.emitters.size(), 1u);

    scene.set_particle_target_point(scene.model_instance_handles[0], 0u, glm::vec3{1.0f, 2.0f, 3.0f});
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
    EXPECT_EQ(scene_particles.owner, nullptr);
    EXPECT_EQ(scene_particles.owner_kind, nw::render::ModelInstanceKind::render_model);
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
    EXPECT_EQ(rebuilt_scene_particles.owner, nullptr);
    EXPECT_EQ(rebuilt_scene_particles.owner_kind, nw::render::ModelInstanceKind::render_model);
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
    ASSERT_EQ(scene.particles.size(), 2u);
    EXPECT_TRUE(has_scene_particle_effect(scene, "base_fx"));
    EXPECT_TRUE(has_scene_particle_effect(scene, "idle_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "impact_fx"));

    ASSERT_TRUE(nw::render::viewer::set_render_model_animation_clip_by_name(scene, "impact", 0.0f));

    ASSERT_EQ(scene.particles.size(), 2u);
    EXPECT_TRUE(has_scene_particle_effect(scene, "base_fx"));
    EXPECT_FALSE(has_scene_particle_effect(scene, "idle_fx"));
    EXPECT_TRUE(has_scene_particle_effect(scene, "impact_fx"));
    ASSERT_EQ(scene.particles.back().import.effect_events.size(), 1u);
    EXPECT_FLOAT_EQ(scene.particles.back().import.effect_events[0].time, 0.0f);
    EXPECT_EQ(scene.particles.back().import.effect_events[0].burst_count, 1u);
}

TEST(RenderViewerParticles, OwnerSourceNodeIndexDrivesEmitterAttachment)
{
    nw::model::Mdl mdl{"test_data/user/development/vfx_lightning_test.mdl"};
    ASSERT_TRUE(mdl.valid());

    nw::render::nwn::ModelLoader loader{nullptr};
    auto model = loader.load(&mdl);
    ASSERT_TRUE(model);

    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));
    scene.rebuild_particles();

    ASSERT_EQ(scene.particles.size(), 1u);
    auto& scene_particles = scene.particles.front();
    ASSERT_FALSE(scene_particles.import.emitter_inits.empty());
    auto& init = scene_particles.import.emitter_inits.front();
    ASSERT_LT(init.emitter, scene_particles.system.emitters.size());
    ASSERT_LT(init.emitter, scene_particles.compiled.effect.emitters.size());
    auto& compiled_attachment = scene_particles.compiled.effect.emitters[init.emitter].attachment;
    ASSERT_EQ(scene_particles.system.emitter_attachments.size(), scene_particles.import.emitter_inits.size());
    auto& binding = scene_particles.system.emitter_attachments.front();
    EXPECT_EQ(binding.emitter, init.emitter);
    EXPECT_EQ(binding.owner_instance_handle, scene_particles.owner_instance_handle);
    EXPECT_EQ(binding.owner_model_index, scene_particles.owner_model_index);
    ASSERT_NE(init.emitter_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    ASSERT_LT(init.emitter_source_node_index, scene_particles.owner->source_nodes_.size());
    ASSERT_NE(init.target_source_node_index, nw::model::kInvalidParticleImportNodeIndex);
    ASSERT_LT(init.target_source_node_index, scene_particles.owner->source_nodes_.size());
    EXPECT_EQ(compiled_attachment.emitter_attachment_point, init.emitter_source_node_index);
    EXPECT_EQ(compiled_attachment.emitter_source_node_index, init.emitter_source_node_index);
    EXPECT_EQ(compiled_attachment.target_attachment_point, init.target_source_node_index);
    EXPECT_EQ(compiled_attachment.target_source_node_index, init.target_source_node_index);
    EXPECT_EQ(compiled_attachment.emitter_node_name, init.emitter_node_name);
    EXPECT_EQ(compiled_attachment.target_node_name, init.target_node_name);
    EXPECT_EQ(binding.emitter_attachment_point, init.emitter_source_node_index);
    EXPECT_EQ(binding.emitter_source_node_index, init.emitter_source_node_index);
    EXPECT_EQ(binding.target_attachment_point, init.target_source_node_index);
    EXPECT_EQ(binding.target_source_node_index, init.target_source_node_index);
    const uint32_t emitter_source_node_index = init.emitter_source_node_index;
    const uint32_t target_source_node_index = init.target_source_node_index;
    const nw::render::ModelAttachmentPointIndex emitter_attachment_point =
        compiled_attachment.emitter_attachment_point;
    const nw::render::ModelAttachmentPointIndex target_attachment_point =
        compiled_attachment.target_attachment_point;

    auto* emitter_node = scene_particles.owner->source_nodes_[emitter_source_node_index];
    ASSERT_NE(emitter_node, nullptr);
    auto* target_node = scene_particles.owner->source_nodes_[target_source_node_index];
    ASSERT_NE(target_node, nullptr);
    init.emitter_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    init.target_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    init.emitter_node_name = "missing_emitter_name";
    init.target_node_name.clear();
    compiled_attachment.emitter_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    compiled_attachment.target_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    compiled_attachment.emitter_node_name = "missing_compiled_emitter_name";
    compiled_attachment.target_node_name.clear();
    binding.emitter_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    binding.target_source_node_index = nw::model::kInvalidParticleImportNodeIndex;
    emitter_node->has_transform_ = true;
    emitter_node->position_ += glm::vec3{2.0f, 3.0f, 4.0f};
    target_node->has_transform_ = true;
    target_node->position_ += glm::vec3{-1.0f, 2.0f, 5.0f};

    scene.update(0);

    const auto* common_instance = scene.model_instances.get(scene.model_instance_handles[0]);
    ASSERT_NE(common_instance, nullptr);
    ASSERT_LT(emitter_attachment_point, common_instance->attachment_node_world_transforms.size());
    ASSERT_LT(emitter_attachment_point, common_instance->attachment_node_transform_valid.size());
    ASSERT_LT(target_attachment_point, common_instance->attachment_node_world_transforms.size());
    ASSERT_LT(target_attachment_point, common_instance->attachment_node_transform_valid.size());
    EXPECT_NE(common_instance->attachment_node_transform_valid[emitter_attachment_point], 0u);
    EXPECT_NE(common_instance->attachment_node_transform_valid[target_attachment_point], 0u);

    const glm::mat4 expected_transform = scene_particles.owner->root_transform() * emitter_node->get_transform();
    const glm::vec3 expected_position = glm::vec3(expected_transform[3]);
    ASSERT_EQ(scene_particles.system.emitter_attachment_frames.size(), scene_particles.import.emitter_inits.size());
    const auto& frame = scene_particles.system.emitter_attachment_frames.front();
    EXPECT_EQ(frame.emitter, init.emitter);
    EXPECT_TRUE(frame.has_emitter_world_transform);
    EXPECT_TRUE(frame.has_target_point);
    const glm::vec3 actual_position = glm::vec3(scene_particles.system.emitters[init.emitter].world_transform[3]);
    EXPECT_NEAR(actual_position.x, expected_position.x, 1.0e-5f);
    EXPECT_NEAR(actual_position.y, expected_position.y, 1.0e-5f);
    EXPECT_NEAR(actual_position.z, expected_position.z, 1.0e-5f);
    EXPECT_NEAR(glm::vec3(frame.emitter_world_transform[3]).x, expected_position.x, 1.0e-5f);
    EXPECT_NEAR(glm::vec3(frame.emitter_world_transform[3]).y, expected_position.y, 1.0e-5f);
    EXPECT_NEAR(glm::vec3(frame.emitter_world_transform[3]).z, expected_position.z, 1.0e-5f);
    EXPECT_EQ(frame.emitter_world_transform, common_instance->attachment_node_world_transforms[emitter_attachment_point]);

    const glm::vec3 expected_target = glm::vec3(scene_particles.owner->root_transform() * target_node->get_transform()[3]);
    const glm::vec3 actual_target = scene_particles.system.emitters[init.emitter].target_point;
    EXPECT_NEAR(actual_target.x, expected_target.x, 1.0e-5f);
    EXPECT_NEAR(actual_target.y, expected_target.y, 1.0e-5f);
    EXPECT_NEAR(actual_target.z, expected_target.z, 1.0e-5f);
    EXPECT_NEAR(frame.target_point.x, expected_target.x, 1.0e-5f);
    EXPECT_NEAR(frame.target_point.y, expected_target.y, 1.0e-5f);
    EXPECT_NEAR(frame.target_point.z, expected_target.z, 1.0e-5f);
    EXPECT_EQ(frame.target_point, glm::vec3(common_instance->attachment_node_world_transforms[target_attachment_point][3]));
}
