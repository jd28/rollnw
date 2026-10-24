#include <gtest/gtest.h>

#include <nw/render/particle_compile.hpp>
#include <nw/render/particle_system.hpp>
#include <nw/render/viewer/preview_model_animation.hpp>
#include <nw/render/viewer/preview_scene.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <memory>
#include <optional>
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
        result.push_back(scene_particles.definition->import.effect.name);
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

void expect_particle_states_equal(const nw::render::viewer::SceneParticleSystem& lhs,
    const nw::render::viewer::SceneParticleSystem& rhs)
{
#define EXPECT_FIELD(field) EXPECT_EQ(lhs.field, rhs.field) << #field
    EXPECT_FIELD(owner_model_index);
    EXPECT_FIELD(owner_instance_handle);
    EXPECT_FIELD(animation_time);
    EXPECT_FIELD(particle_animation_length);
    EXPECT_FIELD(animation_time_initialized);
    EXPECT_FIELD(owner_visible_last);
    EXPECT_FIELD(system.live_particles_per_emitter);
    EXPECT_FIELD(system.emitter_visible);
    EXPECT_FIELD(system.tick_offsets);
    EXPECT_FIELD(system.sort_order);
    EXPECT_FIELD(system.sort_target);
    EXPECT_FIELD(system.sort_keys);
    EXPECT_FIELD(system.particles.has_bezier);
    EXPECT_FIELD(system.particles.has_beams);
    EXPECT_FIELD(system.particles.has_attachment);
    EXPECT_FIELD(system.particles.core.position);
    EXPECT_FIELD(system.particles.core.velocity);
    EXPECT_FIELD(system.particles.core.render_direction);
    EXPECT_FIELD(system.particles.core.age);
    EXPECT_FIELD(system.particles.core.lifetime);
    EXPECT_FIELD(system.particles.core.mass);
    EXPECT_FIELD(system.particles.core.size_x);
    EXPECT_FIELD(system.particles.core.size_y);
    EXPECT_FIELD(system.particles.core.size_x_begin);
    EXPECT_FIELD(system.particles.core.size_x_end);
    EXPECT_FIELD(system.particles.core.size_y_begin);
    EXPECT_FIELD(system.particles.core.size_y_end);
    EXPECT_FIELD(system.particles.core.rotation);
    EXPECT_FIELD(system.particles.core.rotation_rate);
    EXPECT_FIELD(system.particles.core.frame);
    EXPECT_FIELD(system.particles.core.frame_offset);
    EXPECT_FIELD(system.particles.core.color_rgba8);
    EXPECT_FIELD(system.particles.core.color_begin);
    EXPECT_FIELD(system.particles.core.color_end);
    EXPECT_FIELD(system.particles.core.normalized_age);
    EXPECT_FIELD(system.particles.core.emitter_id);
    EXPECT_FIELD(system.particles.core.material_id);
    EXPECT_FIELD(system.particles.bezier.source_position);
    EXPECT_FIELD(system.particles.bezier.target_position);
    EXPECT_FIELD(system.particles.bezier.source_tangent);
    EXPECT_FIELD(system.particles.bezier.target_tangent);
    EXPECT_FIELD(system.particles.beams.source_position);
    EXPECT_FIELD(system.particles.beams.target_position);
    EXPECT_FIELD(system.particles.beams.subdivisions);
    EXPECT_FIELD(system.particles.beams.update_accumulator);
    EXPECT_FIELD(system.particles.beams.jitter_radius);
    EXPECT_FIELD(system.particles.beams.noise_scale);
    EXPECT_FIELD(system.particles.attachment.anchor_position);
    EXPECT_FIELD(system.particles.attachment.local_position);
    EXPECT_EQ(lhs.system.effect, &lhs.definition->compiled.effect);
    EXPECT_EQ(rhs.system.effect, &rhs.definition->compiled.effect);
    ASSERT_EQ(lhs.system.emitters.size(), rhs.system.emitters.size());
    for (size_t i = 0; i < lhs.system.emitters.size(); ++i) {
        EXPECT_FIELD(system.emitters[i].time);
        EXPECT_FIELD(system.emitters[i].spawn_accumulator);
        EXPECT_FIELD(system.emitters[i].distance_accumulator);
        EXPECT_FIELD(system.emitters[i].single_shot_cooldown);
        EXPECT_FIELD(system.emitters[i].pulse_time_remaining);
        EXPECT_FIELD(system.emitters[i].pending_bursts);
        EXPECT_FIELD(system.emitters[i].random_seed);
        EXPECT_FIELD(system.emitters[i].body_spawn_counter);
        EXPECT_FIELD(system.emitters[i].prev_world_pos);
        EXPECT_FIELD(system.emitters[i].frame_delta);
        EXPECT_FIELD(system.emitters[i].linear_velocity);
        EXPECT_FIELD(system.emitters[i].world_transform);
        EXPECT_FIELD(system.emitters[i].inverse_world_transform);
        EXPECT_FIELD(system.emitters[i].target_point);
        EXPECT_FIELD(system.emitters[i].active);
        EXPECT_FIELD(system.emitters[i].sampled_sheet.columns);
        EXPECT_FIELD(system.emitters[i].sampled_sheet.rows);
        EXPECT_FIELD(system.emitters[i].sampled_sheet.frame_begin);
        EXPECT_FIELD(system.emitters[i].sampled_sheet.frame_end);
        EXPECT_FIELD(system.emitters[i].sampled_sheet.frames_per_second);
        EXPECT_FIELD(system.emitters[i].sampled_sheet.random_start);
    }
    ASSERT_EQ(lhs.system.emitter_attachments.size(), rhs.system.emitter_attachments.size());
    for (size_t i = 0; i < lhs.system.emitter_attachments.size(); ++i) {
        EXPECT_FIELD(system.emitter_attachments[i].emitter);
        EXPECT_FIELD(system.emitter_attachments[i].owner_instance_handle);
        EXPECT_FIELD(system.emitter_attachments[i].owner_model_index);
        EXPECT_FIELD(system.emitter_attachments[i].emitter_attachment_point);
        EXPECT_FIELD(system.emitter_attachments[i].target_attachment_point);
    }
    ASSERT_EQ(lhs.system.force_events.size(), rhs.system.force_events.size());
    for (size_t i = 0; i < lhs.system.force_events.size(); ++i) {
        EXPECT_FIELD(system.force_events[i].emitter_id);
        EXPECT_FIELD(system.force_events[i].origin);
        EXPECT_FIELD(system.force_events[i].radius);
        EXPECT_FIELD(system.force_events[i].length);
    }
    ASSERT_EQ(lhs.system.render_packets.packets.size(), rhs.system.render_packets.packets.size());
    for (size_t i = 0; i < lhs.system.render_packets.packets.size(); ++i) {
        EXPECT_FIELD(system.render_packets.packets[i].mode);
        EXPECT_FIELD(system.render_packets.packets[i].semantic);
        EXPECT_FIELD(system.render_packets.packets[i].material);
        EXPECT_FIELD(system.render_packets.packets[i].begin);
        EXPECT_FIELD(system.render_packets.packets[i].count);
        EXPECT_FIELD(system.render_packets.packets[i].blend);
        EXPECT_FIELD(system.render_packets.packets[i].sheet.columns);
        EXPECT_FIELD(system.render_packets.packets[i].sheet.rows);
        EXPECT_FIELD(system.render_packets.packets[i].sheet.frame_begin);
        EXPECT_FIELD(system.render_packets.packets[i].sheet.frame_end);
        EXPECT_FIELD(system.render_packets.packets[i].sheet.frames_per_second);
        EXPECT_FIELD(system.render_packets.packets[i].sheet.random_start);
        EXPECT_FIELD(system.render_packets.packets[i].path.kind);
        EXPECT_FIELD(system.render_packets.packets[i].path.source);
        EXPECT_FIELD(system.render_packets.packets[i].path.control1);
        EXPECT_FIELD(system.render_packets.packets[i].path.control2);
        EXPECT_FIELD(system.render_packets.packets[i].path.target);
        EXPECT_FIELD(system.render_packets.packets[i].path.subdivisions);
        EXPECT_FIELD(system.render_packets.packets[i].double_sided);
        EXPECT_FIELD(system.render_packets.packets[i].transparent);
        EXPECT_FIELD(system.render_packets.packets[i].sort_back_to_front);
        EXPECT_FIELD(system.render_packets.packets[i].preserve_sequence);
        EXPECT_FIELD(system.render_packets.packets[i].uniform_color);
        EXPECT_FIELD(system.render_packets.packets[i].uniform_frame);
        EXPECT_FIELD(system.render_packets.packets[i].uniform_rotation);
        EXPECT_FIELD(system.render_packets.packets[i].tint_to_scene_ambient);
        EXPECT_FIELD(system.render_packets.packets[i].opacity_scale);
        EXPECT_FIELD(system.render_packets.packets[i].blur_length);
        EXPECT_FIELD(system.render_packets.packets[i].deadspace_radians);
        EXPECT_FIELD(system.render_packets.packets[i].max_half_width);
    }
#undef EXPECT_FIELD
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
    EXPECT_EQ(scene_particles.definition->import.effect.name, "render_model_fx");
    ASSERT_EQ(scene_particles.definition->import.effect_events.size(), 1u);
    EXPECT_FLOAT_EQ(scene_particles.definition->import.effect_events[0].time, 0.0f);
    EXPECT_EQ(scene_particles.definition->import.effect_events[0].burst_count, 2u);
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
    EXPECT_EQ(rebuilt_scene_particles.definition->import.effect.name, "render_model_fx");
    ASSERT_EQ(rebuilt_scene_particles.system.emitters.size(), 1u);
    EXPECT_NEAR(rebuilt_scene_particles.system.emitters[0].world_transform[3].x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(rebuilt_scene_particles.system.emitters[0].world_transform[3].y, 4.0f, 1.0e-5f);
    EXPECT_NEAR(rebuilt_scene_particles.system.emitters[0].world_transform[3].z, 5.0f, 1.0e-5f);

    scene.model_instances.destroy(scene.static_model_instance_handles[0]);
    scene.update(1000);

    EXPECT_FALSE(rebuilt_scene_particles.owner_visible_last);
    EXPECT_TRUE(rebuilt_scene_particles.system.particles.core.position.empty());
}

TEST(RenderViewerParticles, AttachmentsUseCurrentNodeTransformsAndAuthoredFallbacks)
{
    auto model = make_render_model_particle_owner();
    auto& emitters = model->particle_systems[0].effect.emitters;
    auto& attachment = emitters[0].attachment;
    attachment.has_default_transform = true;
    attachment.default_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    attachment.target_attachment_point = 1u;
    attachment.has_default_target_offset = true;
    attachment.default_target_offset = {0.0f, 5.0f, 0.0f};

    auto second = emitters[0];
    second.attachment = {};
    second.attachment.has_default_position = true;
    second.attachment.default_position = {0.0f, 2.0f, 0.0f};
    second.attachment.has_default_orientation = true;
    second.attachment.default_orientation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    emitters.push_back(second);

    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));
    auto* owner = scene.static_model_instance(0);
    ASSERT_NE(owner, nullptr);
    owner->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f})
        * glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    owner->attachment_node_world_transforms = {
        glm::translate(glm::mat4{1.0f}, glm::vec3{30.0f, 40.0f, 50.0f}),
        glm::translate(glm::mat4{1.0f}, glm::vec3{60.0f, 70.0f, 80.0f}),
    };
    owner->attachment_node_transform_valid = {1u, 1u};
    auto& states = scene.particles[0].system.emitters;
    ASSERT_EQ(states.size(), 2u);
    states[1].target_point = {9.0f, 8.0f, 7.0f};
    const auto previous_position = states[0].prev_world_pos;

    scene.update(0);
    EXPECT_EQ(glm::vec3(states[0].world_transform[3]), glm::vec3(30.0f, 40.0f, 50.0f));
    EXPECT_EQ(states[0].target_point, glm::vec3(60.0f, 70.0f, 80.0f));
    EXPECT_EQ(states[0].prev_world_pos, previous_position);
    EXPECT_NEAR(states[1].world_transform[3].x, 8.0f, 1.0e-5f);
    EXPECT_NEAR(states[1].world_transform[3].y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(states[1].world_transform[0].x, -1.0f, 1.0e-5f);
    EXPECT_EQ(states[1].target_point, glm::vec3(9.0f, 8.0f, 7.0f));

    // Invalid emitter rows and out-of-range target rows use defaults in owner space.
    owner->attachment_node_transform_valid[0] = 0u;
    owner->attachment_node_world_transforms.resize(1);
    scene.update(0);
    EXPECT_NEAR(states[0].world_transform[3].x, 10.0f, 1.0e-5f);
    EXPECT_NEAR(states[0].world_transform[3].y, 2.0f, 1.0e-5f);
    EXPECT_NEAR(states[0].target_point.x, 5.0f, 1.0e-5f);
    EXPECT_NEAR(states[0].target_point.y, 0.0f, 1.0e-5f);
    EXPECT_EQ(states[1].target_point, glm::vec3(9.0f, 8.0f, 7.0f));
}

TEST(RenderViewerParticles, AttachmentOwnersRejectMismatchedModelsAndReusedHandles)
{
    auto model = make_render_model_particle_owner();
    auto& attachment = model->particle_systems[0].effect.emitters[0].attachment;
    attachment.has_default_transform = true;
    attachment.default_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    nw::render::viewer::PreviewScene scene;
    scene.add(std::move(model));

    nw::render::ModelInstance alternate;
    alternate.render_model_index = 123u;
    alternate.root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{20.0f, 0.0f, 0.0f});
    alternate.attachment_node_world_transforms = {glm::translate(glm::mat4{1.0f}, glm::vec3{30.0f, 0.0f, 0.0f})};
    alternate.attachment_node_transform_valid = {1u};
    const auto alternate_handle = scene.model_instances.create(std::move(alternate));
    auto* owner = scene.static_model_instance(0);
    ASSERT_NE(owner, nullptr);
    owner->root_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f});
    owner->attachment_node_world_transforms[0] = glm::translate(glm::mat4{1.0f}, glm::vec3{13.0f, 4.0f, 5.0f});

    auto& system = scene.particles[0].system;
    auto& binding = system.emitter_attachments[0];
    binding.owner_instance_handle = alternate_handle;
    binding.owner_model_index = 123u;
    scene.update(0);
    EXPECT_FLOAT_EQ(system.emitters[0].world_transform[3].x, 30.0f);

    binding.owner_model_index = 124u;
    scene.update(0);
    EXPECT_FLOAT_EQ(system.emitters[0].world_transform[3].x, 12.0f);

    binding.owner_model_index = 123u;
    scene.model_instances.destroy(alternate_handle);
    nw::render::ModelInstance replacement;
    replacement.render_model_index = 123u;
    replacement.attachment_node_world_transforms = {glm::translate(glm::mat4{1.0f}, glm::vec3{99.0f, 0.0f, 0.0f})};
    replacement.attachment_node_transform_valid = {1u};
    const auto replacement_handle = scene.model_instances.create(std::move(replacement));
    ASSERT_EQ(replacement_handle.index, alternate_handle.index);
    ASSERT_NE(replacement_handle.generation, alternate_handle.generation);
    scene.update(0);
    EXPECT_FLOAT_EQ(system.emitters[0].world_transform[3].x, 12.0f);

    binding.owner_instance_handle = replacement_handle;
    scene.update(0);
    EXPECT_FLOAT_EQ(system.emitters[0].world_transform[3].x, 99.0f);

    // A malformed emitter binding falls back to the compiled attachment on the system owner.
    binding.emitter = nw::render::kInvalidModelNodeIndex;
    scene.update(0);
    EXPECT_EQ(glm::vec3(system.emitters[0].world_transform[3]), glm::vec3(13.0f, 4.0f, 5.0f));
}

TEST(RenderViewerParticles, ShowingParticleOwnerResetsMotionHistoryBeforeUpdating)
{
    nw::render::viewer::PreviewScene scene;
    scene.add(make_render_model_particle_owner());
    auto* owner = scene.static_model_instance(0);
    ASSERT_NE(owner, nullptr);
    scene.update(125);
    auto& emitter = scene.particles[0].system.emitters[0];
    const auto initial_transform = emitter.world_transform;

    owner->visible = false;
    owner->attachment_node_world_transforms[0][3] = {20.0f, 4.0f, 5.0f, 1.0f};
    scene.update(125);
    EXPECT_EQ(emitter.world_transform, initial_transform);

    owner->visible = true;
    scene.update(125);
    EXPECT_FLOAT_EQ(emitter.world_transform[3].x, 20.0f);
    EXPECT_EQ(emitter.linear_velocity, glm::vec3(0.0f));

    owner->attachment_node_world_transforms[0][3].x = 21.0f;
    scene.update(125);
    EXPECT_FLOAT_EQ(emitter.linear_velocity.x, 8.0f);
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
    ASSERT_EQ(scene.particles.front().definition->import.effect_events.size(), 1u);
    EXPECT_FLOAT_EQ(scene.particles.front().definition->import.effect_events[0].time, 0.0f);
    EXPECT_EQ(scene.particles.front().definition->import.effect_events[0].burst_count, 1u);
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
        EXPECT_EQ(particles.system.effect, &particles.definition->compiled.effect);
    }
}

TEST(RenderViewerParticles, SharedDefinitionsKeepSimulationStateIndependent)
{
    std::shared_ptr<nw::render::RenderModel> model = make_render_model_particle_owner();
    nw::render::viewer::PreviewScene scene;
    scene.add(model);
    scene.add(model);
    ASSERT_EQ(scene.particles.size(), 2u);
    auto& first = scene.particles[0];
    auto& second = scene.particles[1];
    EXPECT_EQ(first.definition, second.definition);
    EXPECT_EQ(first.system.effect, second.system.effect);
    EXPECT_NE(first.system.emitters.data(), second.system.emitters.data());
    EXPECT_EQ(scene.particle_definitions.size(), 1u);

    scene.update(125);
    ASSERT_EQ(first.system.particles.core.age.size(), 1u);
    ASSERT_EQ(second.system.particles.core.age.size(), 1u);
    EXPECT_NE(first.system.particles.core.age.data(), second.system.particles.core.age.data());
    first.system.emitters[0].active = false;
    first.system.emitter_visible[0] = 0u;
    const auto first_seed = first.system.emitters[0].random_seed;
    scene.update(125);
    EXPECT_EQ(first.system.particles.core.age.size(), 1u);
    EXPECT_EQ(second.system.particles.core.age.size(), 2u);
    EXPECT_EQ(first.system.emitters[0].random_seed, first_seed);
    EXPECT_EQ(second.system.emitter_visible[0], 1u);
}

TEST(RenderViewerParticles, DefinitionSharingUsesAssetIdentityAndPayloadIndex)
{
    std::shared_ptr<nw::render::RenderModel> model = make_render_model_particle_selection_owner();
    nw::render::viewer::PreviewScene scene;
    scene.add(model);
    scene.add(model);
    const auto idle = scene.particles[0].definition;
    scene.static_model_instance(0u)->animation.clip = 1u;
    const uint32_t selected[] = {0u};
    scene.rebuild_model_particles(selected);
    ASSERT_EQ(scene.particles.size(), 2u);
    EXPECT_EQ(scene.particles[0].definition, idle);
    EXPECT_NE(scene.particles[1].definition, idle);
    EXPECT_EQ(scene.particles[1].definition->import.effect.name, "impact_fx");
    scene.static_model_instance(0u)->animation.clip = 0u;
    scene.rebuild_model_particles(selected);
    EXPECT_EQ(scene.particles[1].definition, idle);

    auto different_asset = make_render_model_particle_selection_owner();
    different_asset->particle_systems[1].effect.emitters[0].emission.rate = 3.0f;
    scene.add(std::move(different_asset));
    EXPECT_NE(scene.particles.back().definition, idle);
    EXPECT_EQ(scene.particles.back().definition->import.effect.name, idle->import.effect.name);
    EXPECT_FLOAT_EQ(scene.particles.back().system.effect->emitters[0].emission.rate, 3.0f);
    EXPECT_FLOAT_EQ(idle->compiled.effect.emitters[0].emission.rate, 8.0f);
}

TEST(RenderViewerParticles, FullRebuildRefreshesDefinitionsWhileMovedRowsRetainOldData)
{
    std::shared_ptr<nw::render::RenderModel> model = make_render_model_particle_owner();
    std::optional<nw::render::viewer::SceneParticleSystem> retained;
    std::weak_ptr<const nw::render::viewer::SceneParticleDefinition> old_definition;
    {
        nw::render::viewer::PreviewScene scene;
        scene.add(model);
        scene.add(model);
        old_definition = scene.particles.front().definition;
        retained.emplace(std::move(scene.particles.front()));
        model->particle_systems[0].effect.emitters[0].emission.rate = 4.0f;
        scene.rebuild_particles();
        ASSERT_EQ(scene.particles.size(), 2u);
        EXPECT_EQ(scene.particles[0].definition, scene.particles[1].definition);
        EXPECT_NE(scene.particles[0].definition, retained->definition);
        EXPECT_FLOAT_EQ(scene.particles[0].system.effect->emitters[0].emission.rate, 4.0f);
        EXPECT_FLOAT_EQ(retained->system.effect->emitters[0].emission.rate, 8.0f);
    }
    model.reset();
    EXPECT_FALSE(old_definition.expired());
    nw::render::tick_particle_system(retained->system, 0.125f);
    EXPECT_EQ(retained->system.particles.core.age.size(), 1u);
    retained.reset();
    EXPECT_TRUE(old_definition.expired());
}

TEST(RenderViewerParticles, ExpiredAssetIdentityCannotReuseALeasedDefinition)
{
    nw::render::viewer::PreviewScene scene;
    std::shared_ptr<nw::render::RenderModel> model = make_render_model_particle_owner();
    scene.add(model);
    auto retained = std::move(scene.particles.front());
    scene.particles.clear();
    const auto* old_address = model.get();
    auto cached = std::move(scene.particle_definitions.at(old_address));
    scene.particle_definitions.clear();
    auto replacement = make_render_model_particle_owner();
    replacement->particle_systems[0].effect.emitters[0].emission.rate = 2.0f;
    scene.static_models[0] = std::move(replacement);
    model.reset();
    ASSERT_TRUE(cached.owner.expired());
    // Simulate allocator address reuse with an old weak owner and live leased data.
    scene.particle_definitions.emplace(scene.static_models[0].get(), std::move(cached));
    const uint32_t selected[] = {0u};
    scene.rebuild_model_particles(selected);
    ASSERT_EQ(scene.particles.size(), 1u);
    EXPECT_NE(scene.particles[0].definition, retained.definition);
    EXPECT_FLOAT_EQ(scene.particles[0].system.effect->emitters[0].emission.rate, 2.0f);
    EXPECT_FLOAT_EQ(retained.system.effect->emitters[0].emission.rate, 8.0f);
}

TEST(RenderViewerParticles, StandaloneDefinitionsSurviveRowMovesAndReleaseWithTheirInstances)
{
    nw::render::viewer::PreviewScene scene;
    scene.add_particle_effect(make_test_particle_effect("standalone"));
    scene.add_particle_effect(make_test_particle_effect("standalone"));
    ASSERT_EQ(scene.particles.size(), 2u);
    EXPECT_NE(scene.particles[0].definition, scene.particles[1].definition);
    const auto* effect = scene.particles[1].system.effect;
    const std::weak_ptr<const nw::render::viewer::SceneParticleDefinition> erased = scene.particles[0].definition;
    const std::weak_ptr<const nw::render::viewer::SceneParticleDefinition> kept = scene.particles[1].definition;
    scene.particles.erase(scene.particles.begin());
    EXPECT_TRUE(erased.expired());
    EXPECT_EQ(scene.particles[0].system.effect, effect);
    scene.update(125);
    EXPECT_EQ(scene.particles[0].system.particles.core.age.size(), 1u);
    scene.particles.clear();
    EXPECT_TRUE(kept.expired());
}

TEST(RenderViewerParticles, ParallelBatchesMatchSerialAcrossStateAndTopologyChanges)
{
    std::shared_ptr<nw::render::RenderModel> model = make_render_model_particle_owner();
    for (size_t count : {0u, 1u, 63u, 64u, 1023u, 1024u, 1025u, 2049u}) {
        SCOPED_TRACE(count);
        nw::render::viewer::PreviewScene serial;
        nw::render::viewer::PreviewScene parallel;
        for (size_t i = 0; i < count; ++i) {
            serial.add(model);
            parallel.add(model);
        }
        constexpr int32_t steps[] = {-16, 0, 16, 33, 125, 1000};
        for (size_t frame = 0; frame < 30; ++frame) {
            SCOPED_TRACE(frame);
            for (auto* scene : {&serial, &parallel}) {
                if (frame == 4 || frame == 8) {
                    for (size_t i = 0; i < scene->static_model_instance_handles.size(); i += 3) {
                        auto* owner = scene->model_instances.get(scene->static_model_instance_handles[i]);
                        ASSERT_NE(owner, nullptr);
                        owner->visible = frame == 8;
                    }
                }
                if (frame == 10 && !scene->static_model_instance_handles.empty()) {
                    scene->model_instances.destroy(scene->static_model_instance_handles[0]);
                }
                if (frame == 6 || frame == 16) {
                    for (auto handle : scene->static_model_instance_handles) {
                        if (auto* owner = scene->model_instances.get(handle)) {
                            owner->root_transform[3].x += static_cast<float>(frame);
                            if (!owner->attachment_node_world_transforms.empty()) {
                                owner->attachment_node_world_transforms[0][3].y += static_cast<float>(frame);
                            }
                        }
                    }
                }
                if (frame == 12) {
                    scene->add(model);
                }
                if (frame == 14) {
                    std::reverse(scene->particles.begin(), scene->particles.end());
                }
                if (frame == 18 && !scene->particles.empty()) {
                    scene->particles.erase(scene->particles.begin());
                }
                if (frame == 24) {
                    scene->particles.clear();
                }
                if (frame == 27) {
                    for (size_t i = 0; i < count; ++i) {
                        scene->add(model);
                    }
                }
            }
            const auto dt = steps[frame % std::size(steps)];
            nw::render::viewer::update_scene_particles(serial, serial.particles, dt);
            parallel.update(dt);
            ASSERT_EQ(serial.particles.size(), parallel.particles.size());
            for (size_t i = 0; i < serial.particles.size(); ++i) {
                SCOPED_TRACE(i);
                expect_particle_states_equal(serial.particles[i], parallel.particles[i]);
            }
        }
    }
}
