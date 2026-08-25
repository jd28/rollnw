#include <gtest/gtest.h>

#include <glm/geometric.hpp>

#include "../tools/client/preview_session.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <ranges>

TEST(ClientPreview, BuildsFixedSamplesWithEdgeInputOnlyOnce)
{
    nw::toolset::PreviewFixedStepState state;
    const nw::toolset::PreviewInputSample frame{
        .move_axis = {0.5f, 1.0f},
        .turn_axis = -0.75f,
        .look_axis = {0.25f, -0.5f},
        .zoom_axis = 1.0f,
        .zoom_delta = 2.0f,
        .click_target = {5.0f, 6.0f, 0.0f},
        .flags = nw::toolset::preview_input_click_target,
    };
    std::array<nw::toolset::PreviewInputSample, 6> samples{};

    const auto stats = nw::toolset::build_preview_tick_samples(
        state, 0.05, frame, samples);

    ASSERT_EQ(stats.status, nw::toolset::PreviewStatus::ok);
    ASSERT_EQ(stats.tick_count, 3u);
    EXPECT_EQ(samples[0].flags, nw::toolset::preview_input_click_target);
    EXPECT_EQ(samples[1].flags, nw::toolset::preview_input_none);
    EXPECT_EQ(samples[2].flags, nw::toolset::preview_input_none);
    EXPECT_EQ(samples[2].move_axis, frame.move_axis);
    EXPECT_FLOAT_EQ(samples[2].turn_axis, frame.turn_axis);
    EXPECT_FLOAT_EQ(samples[0].zoom_delta, frame.zoom_delta);
    EXPECT_FLOAT_EQ(samples[1].zoom_delta, 0.0f);
    EXPECT_FLOAT_EQ(samples[2].zoom_delta, 0.0f);
    EXPECT_LT(state.accumulator, 1.0e-9);
}

TEST(ClientPreview, CapsCatchupAtSixAndKeepsOnlyFractionalTime)
{
    nw::toolset::PreviewFixedStepState state;
    std::array<nw::toolset::PreviewInputSample, 6> samples{};

    const auto stats = nw::toolset::build_preview_tick_samples(
        state, 0.25, {}, samples);

    EXPECT_EQ(stats.status, nw::toolset::PreviewStatus::ok);
    EXPECT_EQ(stats.tick_count, 6u);
    EXPECT_EQ(stats.dropped_tick_count, 0u);
    EXPECT_LT(state.accumulator, 1.0e-9);
}

TEST(ClientPreview, ReportsSmallOutputWithoutRepeatingEdgeInput)
{
    nw::toolset::PreviewFixedStepState state;
    std::array<nw::toolset::PreviewInputSample, 2> samples{};
    const nw::toolset::PreviewInputSample frame{
        .flags = nw::toolset::preview_input_cancel,
    };

    const auto stats = nw::toolset::build_preview_tick_samples(
        state, 0.05, frame, samples);

    EXPECT_EQ(stats.status, nw::toolset::PreviewStatus::output_full);
    EXPECT_EQ(stats.tick_count, 0u);
    EXPECT_DOUBLE_EQ(state.accumulator, 0.0);
}

TEST(ClientPreview, RejectsInvalidFrameDataWithoutMutatingAccumulator)
{
    nw::toolset::PreviewFixedStepState state{.accumulator = 0.005};
    nw::toolset::PreviewInputSample frame;
    frame.move_axis.x = std::numeric_limits<float>::quiet_NaN();
    std::array<nw::toolset::PreviewInputSample, 6> samples{};

    const auto stats = nw::toolset::build_preview_tick_samples(
        state, 0.016, frame, samples);

    EXPECT_EQ(stats.status, nw::toolset::PreviewStatus::invalid_input);
    EXPECT_DOUBLE_EQ(state.accumulator, 0.005);
}

TEST(ClientPreview, AppliesControllerRadialDeadzone)
{
    EXPECT_EQ(nw::toolset::preview_radial_deadzone({0.10f, 0.10f}), glm::vec2{});

    const glm::vec2 full = nw::toolset::preview_radial_deadzone({1.0f, 0.0f});
    EXPECT_FLOAT_EQ(full.x, 1.0f);
    EXPECT_FLOAT_EQ(full.y, 0.0f);

    const glm::vec2 diagonal = nw::toolset::preview_radial_deadzone({0.5f, 0.5f});
    EXPECT_GT(diagonal.x, 0.0f);
    EXPECT_FLOAT_EQ(diagonal.x, diagonal.y);
    EXPECT_LT(glm::length(diagonal), 1.0f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(nw::toolset::preview_radial_deadzone({nan, 0.0f}), glm::vec2{});
}

TEST(ClientPreview, DestroysStoppedSessionAfterKernelShutdown)
{
    nw::toolset::ToolsetPreviewSession session;
    nw::toolset::stop_toolset_preview(session);

    nw::kernel::services().shutdown();
}

TEST(ClientPreview, RunsDetachedActorDeterministicallyAndRestoresLifetimeState)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    const size_t authored_creature_count = area->creatures.size();
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position},
    };
    std::array<nw::toolset::PreviewInputSample, 30> samples{};
    for (auto& sample : samples)
        sample.move_axis.y = 1.0f;

    const auto run = [&]() {
        nw::toolset::ToolsetPreviewSession session;
        const auto started = nw::toolset::start_toolset_preview(session, start);
        EXPECT_TRUE(started.ok()) << started.diagnostic;
        if (!started.ok()) return nw::ObjectSpatialState{};
        EXPECT_TRUE(session.active());
        EXPECT_TRUE(nw::kernel::objects().valid(started.actor));
        EXPECT_EQ(area->creatures.size(), authored_creature_count);
        EXPECT_EQ(std::ranges::find_if(area->creatures, [&](const auto* creature) {
            return creature && creature->handle() == started.actor;
        }),
            area->creatures.end());

        std::array<nw::ObjectSpatialState, 1> output{};
        std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
        const auto ticked = nw::toolset::tick_toolset_preview(
            session, samples, output, locomotion);
        EXPECT_EQ(ticked.status, nw::toolset::PreviewStatus::ok);
        EXPECT_EQ(ticked.sample_count, samples.size());
        EXPECT_EQ(ticked.output_count, 1u);
        EXPECT_GT(ticked.movement_count, 0u);

        const auto final = output[0];
        const auto actor = started.actor;
        nw::toolset::stop_toolset_preview(session);
        EXPECT_FALSE(session.active());
        EXPECT_FALSE(nw::kernel::objects().valid(actor));
        EXPECT_EQ(area->creatures.size(), authored_creature_count);
        return final;
    };

    const auto first = run();
    const auto second = run();
    EXPECT_EQ(first.position, second.position);
    EXPECT_EQ(first.orientation, second.orientation);
    EXPECT_EQ(first.velocity, second.velocity);
    EXPECT_NE(first.position, module->entry_position);
}

TEST(ClientPreview, StartsAndTargetsFromNavigationRays)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    const glm::vec3 ray_origin = module->entry_position + glm::vec3{0.0f, 0.0f, 10.0f};
    const glm::vec3 ray_displacement{0.0f, 0.0f, -20.0f};
    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_ray = {.origin = ray_origin, .displacement = ray_displacement},
        .camera = {.focus = module->entry_position},
        .spawn_source = nw::toolset::PreviewSessionStartInput::SpawnSource::navigation_ray,
    };

    const auto started = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(started.ok()) << started.diagnostic;
    const auto* spatial = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(spatial, nullptr);
    EXPECT_NEAR(spatial->position.x, module->entry_position.x, 0.01f);
    EXPECT_NEAR(spatial->position.y, module->entry_position.y, 0.01f);

    const std::array rays{
        nw::nav::NavRayProjectionInput{.origin = ray_origin, .displacement = ray_displacement},
        nw::nav::NavRayProjectionInput{
            .origin = ray_origin + glm::vec3{1000.0f, 1000.0f, 0.0f},
            .displacement = ray_displacement,
        },
    };
    std::array<nw::nav::NavRayProjectionResult, 2> results{};
    const auto projected = nw::toolset::project_toolset_preview_rays(session, rays, results);
    EXPECT_EQ(projected.input_count, rays.size());
    EXPECT_EQ(projected.output_count, 1u);
    EXPECT_EQ(results[0].status, nw::nav::NavStatus::ok);
    EXPECT_EQ(results[1].status, nw::nav::NavStatus::off_mesh);
    EXPECT_NEAR(results[0].position.x, module->entry_position.x, 0.01f);
    EXPECT_NEAR(results[0].position.y, module->entry_position.y, 0.01f);
}

TEST(ClientPreview, ProducesIdenticalStateAcrossTickBatchBoundaries)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {
            .focus = module->entry_position,
            .yaw = 0.25f,
            .pitch = -0.5f,
            .distance = 12.0f,
        },
    };
    std::array<nw::toolset::PreviewInputSample, 30> samples{};
    for (size_t index = 0; index < samples.size(); ++index) {
        samples[index].move_axis = {0.1f, 0.8f};
        samples[index].look_axis = {0.05f, -0.02f};
        samples[index].zoom_axis = index < 10 ? 0.1f : 0.0f;
    }

    const auto run = [&](std::span<const size_t> batch_sizes) {
        nw::toolset::ToolsetPreviewSession session;
        const auto started = nw::toolset::start_toolset_preview(session, start);
        EXPECT_TRUE(started.ok()) << started.diagnostic;
        nw::ObjectSpatialState spatial{};
        size_t offset = 0;
        for (size_t batch_size : batch_sizes) {
            std::array<nw::ObjectSpatialState, 1> output{};
            std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
            const auto ticked = nw::toolset::tick_toolset_preview(
                session,
                std::span<const nw::toolset::PreviewInputSample>{samples}.subspan(offset, batch_size),
                output,
                locomotion);
            EXPECT_EQ(ticked.status, nw::toolset::PreviewStatus::ok);
            spatial = output[0];
            offset += batch_size;
        }
        EXPECT_EQ(offset, samples.size());
        return std::pair{spatial, session.camera()};
    };

    constexpr std::array<size_t, 1> one_batch{30};
    constexpr std::array<size_t, 4> four_batches{1, 7, 11, 11};
    const auto [one_spatial, one_camera] = run(one_batch);
    const auto [four_spatial, four_camera] = run(four_batches);

    EXPECT_EQ(one_spatial.position, four_spatial.position);
    EXPECT_EQ(one_spatial.orientation, four_spatial.orientation);
    EXPECT_EQ(one_spatial.velocity, four_spatial.velocity);
    EXPECT_EQ(one_camera.focus, four_camera.focus);
    EXPECT_EQ(one_camera.yaw, four_camera.yaw);
    EXPECT_EQ(one_camera.pitch, four_camera.pitch);
    EXPECT_EQ(one_camera.distance, four_camera.distance);
    EXPECT_GT(one_camera.yaw, start.camera.yaw);
    EXPECT_NE(one_camera.pitch, start.camera.pitch);
}

TEST(ClientPreview, CameraOrbitDoesNotRotateHeldMovement)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position, .yaw = 0.25f},
    };
    const auto run = [&](float look) {
        nw::toolset::ToolsetPreviewSession session;
        const auto started = nw::toolset::start_toolset_preview(session, start);
        EXPECT_TRUE(started.ok()) << started.diagnostic;
        std::array<nw::toolset::PreviewInputSample, 30> samples{};
        for (auto& sample : samples) {
            sample.move_axis.y = 1.0f;
            sample.look_axis.x = look;
        }
        std::array<nw::ObjectSpatialState, 1> output{};
        std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
        EXPECT_EQ(nw::toolset::tick_toolset_preview(
                      session, samples, output, locomotion)
                      .status,
            nw::toolset::PreviewStatus::ok);
        return std::pair{output[0], session.camera()};
    };

    const auto [stationary_camera_spatial, stationary_camera] = run(0.0f);
    const auto [orbiting_camera_spatial, orbiting_camera] = run(1.0f);
    EXPECT_EQ(orbiting_camera_spatial.position, stationary_camera_spatial.position);
    EXPECT_EQ(orbiting_camera_spatial.orientation, stationary_camera_spatial.orientation);
    EXPECT_GT(orbiting_camera.yaw, stationary_camera.yaw);
}

TEST(ClientPreview, MovesForwardRelativeToActorFacing)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position, .yaw = 2.5f},
    };
    const auto started = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(started.ok()) << started.diagnostic;
    EXPECT_NEAR(started.stats.actor_clearance, 0.4f, 0.001f);

    const auto* initial_spatial = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(initial_spatial, nullptr);
    const glm::vec3 initial_position = initial_spatial->position;
    const glm::vec3 initial_orientation = initial_spatial->orientation;
    const glm::vec3 initial_facing = glm::normalize(
        glm::vec3{initial_orientation.x, initial_orientation.y, 0.0f});

    std::array<nw::toolset::PreviewInputSample, 30> samples{};
    for (auto& sample : samples)
        sample.move_axis.y = 1.0f;
    std::array<nw::ObjectSpatialState, 1> output{};
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
    ASSERT_EQ(nw::toolset::tick_toolset_preview(
                  session, samples, output, locomotion)
                  .status,
        nw::toolset::PreviewStatus::ok);

    const glm::vec3 displacement = output[0].position - initial_position;
    EXPECT_GT(glm::dot(displacement, initial_facing), 0.0f);
    EXPECT_EQ(output[0].orientation, initial_orientation);
}

TEST(ClientPreview, StrafesWithoutChangingActorFacing)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position, .yaw = 2.5f},
    };
    const auto started = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(started.ok()) << started.diagnostic;

    const auto* initial_spatial = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(initial_spatial, nullptr);
    const glm::vec3 initial_position = initial_spatial->position;
    const glm::vec3 initial_orientation = initial_spatial->orientation;

    std::array<nw::toolset::PreviewInputSample, 30> strafe_samples{};
    for (auto& sample : strafe_samples)
        sample.move_axis.x = 1.0f;
    std::array<nw::ObjectSpatialState, 1> output{};
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
    ASSERT_EQ(nw::toolset::tick_toolset_preview(
                  session, strafe_samples, output, locomotion)
                  .status,
        nw::toolset::PreviewStatus::ok);

    EXPECT_EQ(locomotion[0], nw::toolset::PreviewActorLocomotion::strafing_right);
    const glm::vec3 initial_facing = glm::normalize(
        glm::vec3{initial_orientation.x, initial_orientation.y, 0.0f});
    const glm::vec3 initial_right{initial_facing.y, -initial_facing.x, 0.0f};
    const glm::vec3 displacement = output[0].position - initial_position;
    EXPECT_GT(glm::dot(displacement, initial_right), 0.0f);
    EXPECT_EQ(output[0].orientation, initial_orientation);
}

TEST(ClientPreview, TurnsInPlace)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position},
    };
    const auto started = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(started.ok()) << started.diagnostic;

    const auto* initial_spatial = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(initial_spatial, nullptr);
    const glm::vec3 initial_position = initial_spatial->position;
    const glm::vec3 initial_orientation = initial_spatial->orientation;

    std::array<nw::toolset::PreviewInputSample, 30> turn_samples{};
    for (auto& sample : turn_samples)
        sample.turn_axis = -1.0f;
    std::array<nw::ObjectSpatialState, 1> output{};
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
    ASSERT_EQ(nw::toolset::tick_toolset_preview(
                  session, turn_samples, output, locomotion)
                  .status,
        nw::toolset::PreviewStatus::ok);

    EXPECT_EQ(locomotion[0], nw::toolset::PreviewActorLocomotion::turning_left);
    EXPECT_EQ(output[0].position, initial_position);
    EXPECT_NE(output[0].orientation, initial_orientation);
    EXPECT_EQ(output[0].velocity, glm::vec3{});

    const glm::vec3 initial_facing = glm::normalize(
        glm::vec3{initial_orientation.x, initial_orientation.y, 0.0f});
    const glm::vec3 left_facing = glm::normalize(
        glm::vec3{output[0].orientation.x, output[0].orientation.y, 0.0f});
    EXPECT_GT(initial_facing.x * left_facing.y - initial_facing.y * left_facing.x, 0.0f);

    for (auto& sample : turn_samples)
        sample.turn_axis = 1.0f;
    ASSERT_EQ(nw::toolset::tick_toolset_preview(
                  session, turn_samples, output, locomotion)
                  .status,
        nw::toolset::PreviewStatus::ok);
    EXPECT_EQ(locomotion[0], nw::toolset::PreviewActorLocomotion::turning_right);
    const glm::vec3 right_facing = glm::normalize(
        glm::vec3{output[0].orientation.x, output[0].orientation.y, 0.0f});
    EXPECT_LT(left_facing.x * right_facing.y - left_facing.y * right_facing.x, 0.0f);
}

TEST(ClientPreview, RejectsTickOutputBeforeAdvancingSession)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position},
    };
    const auto started = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(started.ok()) << started.diagnostic;

    const auto* before = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(before, nullptr);
    const nw::ObjectSpatialState initial = *before;
    const std::array samples{nw::toolset::PreviewInputSample{.move_axis = {0.0f, 1.0f}}};
    const auto ticked = nw::toolset::tick_toolset_preview(session, samples, {}, {});
    EXPECT_EQ(ticked.status, nw::toolset::PreviewStatus::output_full);

    const auto* after = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->position, initial.position);
    EXPECT_EQ(after->orientation, initial.orientation);
    EXPECT_EQ(after->velocity, initial.velocity);
}

TEST(ClientPreview, RepeatsLifecycleWithoutGrowingLiveState)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    const auto& objects = nw::kernel::objects();
    const size_t object_count = objects.object_count();
    const size_t tag_count = objects.tag_count();
    const auto component_stats = objects.components().stats();
    nw::Vector<nw::ObjectHandle> authored_creatures;
    authored_creatures.reserve(area->creatures.size());
    for (const auto* creature : area->creatures) {
        ASSERT_NE(creature, nullptr);
        authored_creatures.push_back(creature->handle());
    }

    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position},
    };
    const std::array samples{nw::toolset::PreviewInputSample{.move_axis = {0.0f, 1.0f}}};
    size_t navigation_payload_bytes = 0;

    for (size_t cycle = 0; cycle < 100; ++cycle) {
        nw::toolset::ToolsetPreviewSession session;
        const auto started = nw::toolset::start_toolset_preview(session, start);
        ASSERT_TRUE(started.ok()) << "cycle " << cycle << ": " << started.diagnostic;
        if (cycle == 0) {
            navigation_payload_bytes = started.stats.navigation_payload_bytes;
            ASSERT_GT(navigation_payload_bytes, 0u);
        } else {
            EXPECT_EQ(started.stats.navigation_payload_bytes, navigation_payload_bytes);
        }

        std::array<nw::ObjectSpatialState, 1> output{};
        std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
        EXPECT_EQ(nw::toolset::tick_toolset_preview(
                      session, samples, output, locomotion)
                      .status,
            nw::toolset::PreviewStatus::ok);
        const auto actor = started.actor;
        nw::toolset::stop_toolset_preview(session);

        EXPECT_FALSE(objects.valid(actor));
        EXPECT_EQ(objects.object_count(), object_count) << "cycle " << cycle;
        EXPECT_EQ(objects.tag_count(), tag_count) << "cycle " << cycle;
        EXPECT_EQ(objects.components().stats(), component_stats) << "cycle " << cycle;
        ASSERT_EQ(area->creatures.size(), authored_creatures.size());
        for (size_t index = 0; index < authored_creatures.size(); ++index) {
            ASSERT_NE(area->creatures[index], nullptr);
            EXPECT_EQ(area->creatures[index]->handle(), authored_creatures[index]);
        }
    }
}
