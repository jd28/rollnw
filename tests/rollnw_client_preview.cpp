#include <gtest/gtest.h>

#include <glm/geometric.hpp>

#include "../tools/client/area_navigation.hpp"
#include "../tools/client/object_edits.hpp"
#include "../tools/client/pc_input.hpp"
#include "../tools/client/preview_session.hpp"
#include "../tools/client/runtime_input.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ModelCache.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/component_propset_json.hpp>

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

TEST(ClientPreview, BuildsDenseDoorClickEdgeOnlyOnce)
{
    nw::toolset::PreviewFixedStepState state;
    const nw::toolset::PreviewInputSample frame{
        .door_bounds_min = {4.0f, 5.0f, 0.0f},
        .door_bounds_max = {6.0f, 6.0f, 3.0f},
        .door_index = 3,
        .flags = nw::toolset::preview_input_click_door,
    };
    std::array<nw::toolset::PreviewInputSample, 6> samples{};

    const auto stats = nw::toolset::build_preview_tick_samples(
        state, 0.05, frame, samples);

    ASSERT_EQ(stats.status, nw::toolset::PreviewStatus::ok);
    ASSERT_EQ(stats.tick_count, 3u);
    EXPECT_EQ(samples[0].flags, nw::toolset::preview_input_click_door);
    EXPECT_EQ(samples[0].door_index, 3u);
    EXPECT_EQ(samples[0].door_bounds_min, frame.door_bounds_min);
    EXPECT_EQ(samples[0].door_bounds_max, frame.door_bounds_max);
    EXPECT_EQ(samples[1].flags, nw::toolset::preview_input_none);
    EXPECT_EQ(samples[2].flags, nw::toolset::preview_input_none);
}

TEST(ClientPreview, OpenDoorHitFallsThroughToClickNavigation)
{
    const std::array doors{
        nw::toolset::PreviewDoorVisualState{
            .state = nw::toolset::PreviewDoorState::open1,
        },
        nw::toolset::PreviewDoorVisualState{
            .state = nw::toolset::PreviewDoorState::closed,
        },
        nw::toolset::PreviewDoorVisualState{
            .state = nw::toolset::PreviewDoorState::open2,
        },
    };

    EXPECT_FALSE(nw::toolset::preview_door_requires_interaction(doors, 0));
    EXPECT_TRUE(nw::toolset::preview_door_requires_interaction(doors, 1));
    EXPECT_FALSE(nw::toolset::preview_door_requires_interaction(doors, 2));
    EXPECT_FALSE(nw::toolset::preview_door_requires_interaction(doors, 3));
}

TEST(ClientPreview, LatestPointerActionReplacesPendingDoorOrTarget)
{
    nw::toolset::PreviewInputSample sample;
    ASSERT_TRUE(nw::toolset::set_preview_click_door(
        sample, 2, {1.0f, 2.0f, 0.0f}, {2.0f, 3.0f, 2.0f}));
    EXPECT_EQ(sample.flags, nw::toolset::preview_input_click_door);
    EXPECT_EQ(sample.door_index, 2u);

    ASSERT_TRUE(nw::toolset::set_preview_click_target(
        sample, {9.0f, 8.0f, 1.0f}));
    EXPECT_EQ(sample.flags, nw::toolset::preview_input_click_target);
    EXPECT_EQ(sample.click_target, glm::vec3(9.0f, 8.0f, 1.0f));
    EXPECT_EQ(sample.door_index, UINT32_MAX);

    ASSERT_TRUE(nw::toolset::set_preview_click_door(
        sample, 4, {3.0f, 4.0f, 0.0f}, {4.0f, 5.0f, 2.0f}));
    EXPECT_EQ(sample.flags, nw::toolset::preview_input_click_door);
    EXPECT_EQ(sample.click_target, glm::vec3(0.0f));

    EXPECT_FALSE(nw::toolset::set_preview_click_target(
        sample, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}));
    EXPECT_EQ(sample.flags, nw::toolset::preview_input_none);
}

TEST(ClientPreview, RejectsReversedDoorClickBounds)
{
    nw::toolset::PreviewFixedStepState state{.accumulator = 0.005};
    const nw::toolset::PreviewInputSample frame{
        .door_bounds_min = {2.0f, 0.0f, 0.0f},
        .door_bounds_max = {1.0f, 1.0f, 1.0f},
        .door_index = 0,
        .flags = nw::toolset::preview_input_click_door,
    };
    std::array<nw::toolset::PreviewInputSample, 6> samples{};

    EXPECT_EQ(nw::toolset::build_preview_tick_samples(
                  state, 0.05, frame, samples)
                  .status,
        nw::toolset::PreviewStatus::invalid_input);
    EXPECT_DOUBLE_EQ(state.accumulator, 0.005);
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
        const auto* actor_spatial
            = nw::kernel::objects().components().find_spatial(started.actor);
        EXPECT_NE(actor_spatial, nullptr);
        if (!actor_spatial) return nw::ObjectSpatialState{};
        EXPECT_FLOAT_EQ(actor_spatial->movement_rate, 1.75f);
        EXPECT_FLOAT_EQ(started.stats.walk_rate, actor_spatial->movement_rate);
        EXPECT_FALSE(started.stats.walk_rate_fallback);
        EXPECT_FALSE(started.stats.movement_disabled);
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
    constexpr float camera_yaw = 1.25f;
    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_ray = {.origin = ray_origin, .displacement = ray_displacement},
        .camera = {
            .focus = module->entry_position,
            .yaw = camera_yaw,
        },
        .spawn_source = nw::toolset::PreviewSessionStartInput::SpawnSource::navigation_ray,
    };

    const auto started = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(started.ok()) << started.diagnostic;
    const auto* spatial = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(spatial, nullptr);
    EXPECT_NEAR(spatial->position.x, module->entry_position.x, 0.01f);
    EXPECT_NEAR(spatial->position.y, module->entry_position.y, 0.01f);
    EXPECT_NEAR(spatial->orientation.x, std::cos(camera_yaw), 1.0e-6f);
    EXPECT_NEAR(spatial->orientation.y, std::sin(camera_yaw), 1.0e-6f);
    EXPECT_NEAR(spatial->orientation.z, 0.0f, 1.0e-6f);

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

TEST(ClientPreview, StartsFromNavigationRaysOutsideAreaBounds)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    const auto target = module->entry_position;
    const float max_x = static_cast<float>(area->width) * nw::nav::nav_tile_size;
    const float max_y = static_cast<float>(area->height) * nw::nav::nav_tile_size;
    const std::array origins{
        glm::vec3{-10.0f, target.y, target.z + 10.0f},
        glm::vec3{max_x + 10.0f, target.y, target.z + 10.0f},
        glm::vec3{target.x, -10.0f, target.z + 10.0f},
        glm::vec3{target.x, max_y + 10.0f, target.z + 10.0f},
    };
    const auto authored_creature_count = area->creatures.size();
    nw::toolset::ToolsetPreviewSession session;
    for (size_t index = 0; index < origins.size(); ++index) {
        SCOPED_TRACE(index);
        const auto started = nw::toolset::start_toolset_preview(session, {
                                                                             .area = area->handle(),
                                                                             .actor = {nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
                                                                             .spawn_ray = {
                                                                                 .origin = origins[index],
                                                                                 .displacement = (target - origins[index]) * 2.0f,
                                                                             },
                                                                             .spawn_source = nw::toolset::PreviewSessionStartInput::SpawnSource::navigation_ray,
                                                                         });
        ASSERT_TRUE(started.ok()) << started.diagnostic;
        const auto* spatial = nw::kernel::objects().components().find_spatial(started.actor);
        ASSERT_NE(spatial, nullptr);
        EXPECT_NEAR(spatial->position.x, target.x, 0.25f);
        EXPECT_NEAR(spatial->position.y, target.y, 0.25f);
        EXPECT_EQ(area->creatures.size(), authored_creature_count);
        nw::toolset::stop_toolset_preview(session);
        EXPECT_FALSE(nw::kernel::objects().valid(started.actor));
    }
}

TEST(ClientPreview, OpensClosesAndReopensReplacementDoor)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/module_as_dir/");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);
    ASSERT_EQ(area->doors.size(), 1u);

    const auto removed_door = area->doors.front()->handle();
    const auto* removed_spatial
        = nw::kernel::objects().components().find_spatial(removed_door);
    ASSERT_NE(removed_spatial, nullptr);
    const nw::ObjectSpatialState replacement_spatial = *removed_spatial;

    nw::toolset::CommandContext context;
    const std::array removed{removed_door};
    auto deleted = nw::toolset::delete_area_objects(
        area->handle(), removed, "Delete Door", context);
    ASSERT_TRUE(deleted.ok()) << deleted.message;
    ASSERT_TRUE(area->doors.empty());

    const std::array placements{nw::toolset::AreaObjectBlueprintPlacement{
        .resource = {nw::Resref{"door_ttr_002"}, nw::ResourceType::utd},
        .transform = {
            .position = replacement_spatial.position,
            .orientation = replacement_spatial.orientation,
            .scale = replacement_spatial.scale,
        },
    }};
    const auto loaded = nw::toolset::load_area_object_blueprints(
        area->handle(), placements);
    ASSERT_TRUE(loaded.ok()) << loaded.diagnostic;
    ASSERT_EQ(loaded.objects.size(), 1u);
    const auto replacement_door = loaded.objects.front();
    ASSERT_NE(replacement_door, removed_door);

    auto placed = nw::toolset::place_area_objects(
        area->handle(), loaded.objects, "Place Door", context);
    ASSERT_TRUE(placed.ok()) << placed.message;
    ASSERT_EQ(area->doors.size(), 1u);
    ASSERT_EQ(area->doors.front()->handle(), replacement_door);
    const size_t authored_creature_count = area->creatures.size();

    nw::toolset::ToolsetPreviewSession session;
    const nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = {nw::Resref{"test_creature"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
        .camera = {.focus = module->entry_position},
    };
    const auto started = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(started.ok()) << started.diagnostic;
    const auto* actor_spatial = nw::kernel::objects().components().find_spatial(started.actor);
    ASSERT_NE(actor_spatial, nullptr);
    const glm::vec3 initial_position = actor_spatial->position;

    const auto handles = nw::toolset::toolset_preview_door_handles(session);
    ASSERT_EQ(handles.size(), 1u);
    EXPECT_EQ(handles.front(), replacement_door);
    EXPECT_NE(handles.front(), removed_door);
    const auto visual_states
        = nw::toolset::toolset_preview_door_visual_states(session);
    ASSERT_EQ(visual_states.size(), 1u);
    EXPECT_EQ(visual_states.front().door, replacement_door);
    EXPECT_TRUE(nw::toolset::preview_door_requires_interaction(
        visual_states, 0));

    nw::toolset::PreviewInputSample sample;
    ASSERT_TRUE(nw::toolset::set_preview_click_door(
        sample,
        0,
        replacement_spatial.position - glm::vec3{1.0f},
        replacement_spatial.position + glm::vec3{1.0f}));
    std::array<nw::ObjectSpatialState, 1> spatial_output;
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion_output;
    bool opened = false;
    for (size_t tick = 0; tick < 10'000 && !opened; ++tick) {
        const auto advanced = nw::toolset::tick_toolset_preview(
            session,
            std::span<const nw::toolset::PreviewInputSample>{&sample, 1},
            spatial_output,
            locomotion_output);
        ASSERT_EQ(advanced.status, nw::toolset::PreviewStatus::ok);
        sample = {};
        opened = nw::toolset::toolset_preview_door_visual_states(session)[0].state
            != nw::toolset::PreviewDoorState::closed;
    }
    ASSERT_TRUE(opened);

    nw::toolset::AreaNavigationSource navigation;
    std::string navigation_diagnostic;
    ASSERT_TRUE(nw::toolset::build_area_navigation_source(
        *area, navigation, navigation_diagnostic))
        << navigation_diagnostic;
    const auto door_row = std::ranges::find(
        navigation.doors, replacement_door, &nw::nav::NavDoorObstacleRow::door);
    ASSERT_NE(door_row, navigation.doors.end());
    const float actor_side = glm::dot(
                                 spatial_output[0].position - door_row->position, door_row->normal)
            < 0.0f
        ? -1.0f
        : 1.0f;
    constexpr std::array<float, 4> target_distances{2.0f, 4.0f, 6.0f, 8.0f};
    std::array<nw::nav::NavRayProjectionInput, target_distances.size()> rays{};
    for (size_t index = 0; index < rays.size(); ++index) {
        rays[index] = {
            .origin = door_row->position
                - actor_side * door_row->normal * target_distances[index]
                + glm::vec3{0.0f, 0.0f, 5.0f},
            .displacement = {0.0f, 0.0f, -10.0f},
        };
    }
    std::array<nw::nav::NavRayProjectionResult, rays.size()> projected{};
    nw::toolset::project_toolset_preview_rays(session, rays, projected);
    glm::vec3 target{0.0f};
    bool found_target = false;
    for (size_t index = projected.size(); index > 0; --index) {
        if (projected[index - 1].status != nw::nav::NavStatus::ok) continue;
        target = projected[index - 1].position;
        found_target = true;
        break;
    }
    ASSERT_TRUE(found_target);
    ASSERT_TRUE(nw::toolset::set_preview_click_target(sample, target));
    bool reached_target = false;
    for (size_t tick = 0; tick < 10'000 && !reached_target; ++tick) {
        const auto advanced = nw::toolset::tick_toolset_preview(
            session,
            std::span<const nw::toolset::PreviewInputSample>{&sample, 1},
            spatial_output,
            locomotion_output);
        ASSERT_EQ(advanced.status, nw::toolset::PreviewStatus::ok);
        sample = {};
        const glm::vec2 distance{
            spatial_output[0].position.x - target.x,
            spatial_output[0].position.y - target.y,
        };
        reached_target = glm::dot(distance, distance) < 0.1f;
    }
    ASSERT_TRUE(reached_target);

    // Request both transitions explicitly: this fixture also has a route
    // around the closed door, so a return target alone need not reopen it.
    for (const bool closing : {true, false}) {
        SCOPED_TRACE(closing);
        ASSERT_TRUE(nw::toolset::set_preview_click_door(
            sample, 0,
            replacement_spatial.position - glm::vec3{1.0f},
            replacement_spatial.position + glm::vec3{1.0f}));
        size_t transitions = 0;
        for (size_t tick = 0; tick < 10'000 && transitions == 0; ++tick) {
            const auto advanced = nw::toolset::tick_toolset_preview(
                session, std::span{&sample, 1}, spatial_output, locomotion_output);
            ASSERT_EQ(advanced.status, nw::toolset::PreviewStatus::ok);
            ASSERT_EQ(advanced.path_failure_count, 0u);
            ASSERT_EQ(advanced.door_close_blocked_count, 0u);
            EXPECT_EQ(closing ? advanced.door_open_count : advanced.door_close_count, 0u);
            sample = {};
            const size_t changed = closing ? advanced.door_close_count : advanced.door_open_count;
            transitions += changed;
            if (changed != 0) {
                EXPECT_GT(advanced.rebuilt_tile_count, 0u);
                const auto& visual = nw::toolset::toolset_preview_door_visual_states(session)[0];
                EXPECT_EQ(visual.state == nw::toolset::PreviewDoorState::closed, closing);
                EXPECT_TRUE(visual.transition);
            }
        }
        ASSERT_EQ(transitions, 1u);
    }

    // Navigation remains usable after both tile rebuilds.
    ASSERT_TRUE(nw::toolset::set_preview_click_target(sample, initial_position));
    bool returned = false;
    for (size_t tick = 0; tick < 10'000 && !returned; ++tick) {
        const auto advanced = nw::toolset::tick_toolset_preview(
            session, std::span{&sample, 1}, spatial_output, locomotion_output);
        ASSERT_EQ(advanced.status, nw::toolset::PreviewStatus::ok);
        ASSERT_EQ(advanced.path_failure_count, 0u);
        sample = {};
        EXPECT_EQ(advanced.door_open_count, 0u);
        EXPECT_EQ(advanced.door_close_count, 0u);
        const glm::vec2 distance = glm::vec2{spatial_output[0].position - initial_position};
        returned = glm::dot(distance, distance) < 0.1f;
    }
    EXPECT_TRUE(returned);
    EXPECT_EQ(area->creatures.size(), authored_creature_count);
    nw::toolset::stop_toolset_preview(session);
    EXPECT_FALSE(nw::kernel::objects().valid(started.actor));

    nw::toolset::AreaNavigationSource authored_navigation;
    ASSERT_TRUE(nw::toolset::build_area_navigation_source(
        *area, authored_navigation, navigation_diagnostic))
        << navigation_diagnostic;
    ASSERT_EQ(authored_navigation.doors.size(), 1u);
    EXPECT_EQ(authored_navigation.doors[0].door, replacement_door);
    EXPECT_EQ(authored_navigation.doors[0].state, door_row->state);
}

TEST(ClientPreview, RestartsAfterRejectedPlacementRay)
{
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);

    nw::toolset::ToolsetPreviewSession session;
    nw::toolset::PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_ray = {
            .origin = module->entry_position
                + glm::vec3{1000.0f, 1000.0f, 10.0f},
            .displacement = {0.0f, 0.0f, -20.0f},
        },
        .camera = {.focus = module->entry_position},
        .spawn_source = nw::toolset::PreviewSessionStartInput::SpawnSource::navigation_ray,
    };

    const auto rejected = nw::toolset::start_toolset_preview(session, start);
    EXPECT_FALSE(rejected.ok());
    EXPECT_EQ(rejected.status, nw::toolset::PreviewStatus::navigation_failed);
    EXPECT_EQ(rejected.diagnostic,
        "Preview placement ray did not hit walkable navigation geometry");
    EXPECT_FALSE(session.active());

    start.spawn_ray.origin = module->entry_position
        + glm::vec3{0.0f, 0.0f, 10.0f};
    const auto restarted = nw::toolset::start_toolset_preview(session, start);
    ASSERT_TRUE(restarted.ok()) << restarted.diagnostic;
    EXPECT_TRUE(session.active());
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

TEST(ClientPreview, NavigationDebugSnapshotTracksRouteAndToggleLifetime)
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
    EXPECT_FALSE(nw::toolset::toolset_preview_navigation_debug(session).enabled);

    ASSERT_EQ(nw::toolset::set_toolset_preview_navigation_debug(session, true),
        nw::toolset::PreviewStatus::ok);
    const auto enabled = nw::toolset::toolset_preview_navigation_debug(session);
    ASSERT_TRUE(enabled.enabled);
    ASSERT_FALSE(enabled.triangles.empty());
    const uint64_t initial_revision = enabled.revision;

    const std::array samples{nw::toolset::PreviewInputSample{
        .click_target = module->entry_position + glm::vec3{1.0f, 0.0f, 0.0f},
        .flags = nw::toolset::preview_input_click_target,
    }};
    std::array<nw::ObjectSpatialState, 1> spatial{};
    std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion{};
    ASSERT_EQ(nw::toolset::tick_toolset_preview(
                  session, samples, spatial, locomotion)
                  .status,
        nw::toolset::PreviewStatus::ok);
    const auto routed = nw::toolset::toolset_preview_navigation_debug(session);
    EXPECT_TRUE(routed.has_requested_target);
    EXPECT_GT(routed.revision, initial_revision);

    ASSERT_EQ(nw::toolset::set_toolset_preview_navigation_debug(session, false),
        nw::toolset::PreviewStatus::ok);
    EXPECT_FALSE(nw::toolset::toolset_preview_navigation_debug(session).enabled);
    nw::toolset::stop_toolset_preview(session);
    EXPECT_EQ(nw::toolset::set_toolset_preview_navigation_debug(session, true),
        nw::toolset::PreviewStatus::idle);
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
    nw::toolset::ToolsetPreviewSession session;

    for (size_t cycle = 0; cycle < 100; ++cycle) {
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

TEST(ClientPreview, ItemLoopPreservesIdentityAndAuthoredStateAcrossRestarts)
{
    using namespace nw::toolset;
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);
    auto* source = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(source, nullptr);
    area->items.push_back(source);
    auto& components = nw::kernel::objects().components();
    ASSERT_TRUE(components.set_area(source->handle(), area->handle().id));
    ASSERT_TRUE(components.set_position(source->handle(), module->entry_position));
    const auto source_handle = source->handle();
    const size_t authored_count = area->items.size();
    const size_t creature_count = area->creatures.size();
    nlohmann::json before;
    ASSERT_TRUE(nw::object_to_component_propset_json(source, before, &nw::kernel::runtime()));
    const PreviewSessionStartInput start{
        .area = area->handle(),
        .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
        .spawn_position = module->entry_position,
    };
    nw::ObjectHandle previous_item{};
    const auto object_count = nw::kernel::objects().object_count();
    for (int repeat = 0; repeat < 3; ++repeat) {
        SCOPED_TRACE(repeat);
        ToolsetPreviewSession session;
        const auto started = start_toolset_preview(session, start);
        ASSERT_TRUE(started.ok()) << started.diagnostic;
        auto world = toolset_preview_item_world(session);
        ASSERT_EQ(world.ground.size(), authored_count);
        const auto item = world.ground.back();
        const auto runtime_area = world.area;
        EXPECT_NE(item, source_handle);
        EXPECT_NE(item, previous_item);
        EXPECT_NE(runtime_area, area->handle());
        auto* actor = nw::kernel::objects().get<nw::Creature>(started.actor);
        ASSERT_NE(actor, nullptr);
        auto* clone = nw::kernel::objects().get<nw::Item>(item);
        ASSERT_NE(clone, nullptr);
        const auto apply = [&](PreviewItemAction action, int32_t slot = -1) {
            PreviewInputSample sample;
            EXPECT_TRUE(set_preview_item_action(sample, {item, slot, action}));
            std::array<nw::ObjectSpatialState, 1> spatial;
            std::array<PreviewActorLocomotion, 1> locomotion;
            auto result = tick_toolset_preview(session, {&sample, 1}, spatial, locomotion).action_result;
            sample = {};
            for (int i = 0; i < 120 && !result.changed && session.active()
                && toolset_preview_action_state(session).phase != nw::gameplay::ActionPhase::finished;
                ++i) {
                result = tick_toolset_preview(session, {&sample, 1}, spatial, locomotion).action_result;
            }
            return result;
        };
        EXPECT_TRUE(apply(PreviewItemAction::pickup).ok());
        ASSERT_TRUE(actor->inventory().has_item(clone));
        const auto rejected = apply(PreviewItemAction::equip, static_cast<int32_t>(nw::EquipIndex::head));
        EXPECT_FALSE(rejected.ok());
        EXPECT_FALSE(rejected.changed);
        EXPECT_TRUE(actor->inventory().has_item(clone));
        const auto effects_before = actor->effects().size();
        const auto equipped = apply(PreviewItemAction::equip, static_cast<int32_t>(nw::EquipIndex::belt));
        ASSERT_TRUE(equipped.ok()) << preview_item_message(equipped);
        EXPECT_TRUE(equipped.equipment_changed);
        EXPECT_GT(actor->effects().size(), effects_before);
        EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), clone);
        EXPECT_FALSE(actor->inventory().has_item(clone));
        EXPECT_FALSE(apply(PreviewItemAction::drop).ok());
        ASSERT_TRUE(apply(PreviewItemAction::unequip, static_cast<int32_t>(nw::EquipIndex::belt)).ok());
        EXPECT_EQ(actor->effects().size(), effects_before);
        ASSERT_TRUE(apply(PreviewItemAction::drop).ok());
        EXPECT_EQ(components.find_spatial(item)->area, runtime_area.id);
        EXPECT_EQ(components.find_spatial(item)->position, components.find_spatial(started.actor)->position);
        ASSERT_TRUE(apply(PreviewItemAction::pickup).ok());
        if (repeat == 1) { ASSERT_TRUE(apply(PreviewItemAction::equip, static_cast<int32_t>(nw::EquipIndex::belt)).ok()); }
        if (repeat == 2) { ASSERT_TRUE(apply(PreviewItemAction::drop).ok()); }
        stop_toolset_preview(session);
        EXPECT_FALSE(nw::kernel::objects().valid(item));
        EXPECT_FALSE(nw::kernel::objects().valid(runtime_area));
        EXPECT_FALSE(nw::kernel::objects().valid(started.actor));
        EXPECT_EQ(nw::kernel::objects().object_count(), object_count);
        EXPECT_TRUE(nw::kernel::objects().valid(source_handle));
        EXPECT_EQ(area->items.size(), authored_count);
        EXPECT_EQ(area->creatures.size(), creature_count);
        EXPECT_EQ(area->items.back()->handle(), source_handle);
        nlohmann::json after;
        ASSERT_TRUE(nw::object_to_component_propset_json(source, after, &nw::kernel::runtime()));
        EXPECT_EQ(after, before);
        EXPECT_EQ(apply(PreviewItemAction::pickup).status, PreviewItemStatus::inactive);
        previous_item = item;
    }
}

namespace {

class ClientPickupActions : public ::testing::Test {
protected:
    bool fixture_rig = false;
    nw::toolset::ToolsetPreviewSession session;
    nw::ObjectHandle item{};
    nw::ObjectSpatialState spatial;
    glm::vec3 start_position{};
    glm::vec3 item_position{};

    void SetUp() override
    {
        auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
        ASSERT_NE(module, nullptr);
        auto* area = module->get_area(0);
        ASSERT_NE(area, nullptr);
        // The dedicated distribution indexes pmh0 but has no model payload.
        // Supply a real supermodel chain for these headless timing checks only;
        // desktop runs continue to use the installed rig.
        if (nw::kernel::resman().demand({nw::Resref{"pmh0"}, nw::ResourceType::mdl}).bytes.size() == 0) {
            auto rig = std::make_unique<nw::model::Mdl>(std::filesystem::path{"test_data/renderer/pickup_rig.mdl"});
            ASSERT_TRUE(rig->valid());
            fixture_rig = nw::kernel::models().map_.emplace("pmh0", nw::kernel::ModelPayload{std::move(rig), 1}).second;
            ASSERT_TRUE(fixture_rig);
        }
        auto* source = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
        ASSERT_NE(source, nullptr);
        area->items.push_back(source);
        auto& components = nw::kernel::objects().components();
        ASSERT_TRUE(components.set_area(source->handle(), area->handle().id));
        ASSERT_TRUE(components.set_position(source->handle(), module->entry_position));
        const auto started = nw::toolset::start_toolset_preview(session, {
                                                                             .area = area->handle(),
                                                                             .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
                                                                             .spawn_position = module->entry_position,
                                                                         });
        ASSERT_TRUE(started.ok()) << started.diagnostic;
        start_position = components.find_spatial(started.actor)->position;
        item = nw::toolset::toolset_preview_item_world(session).ground.back();
        const std::array offsets{glm::vec3{4, 0, 10}, glm::vec3{-4, 0, 10}, glm::vec3{0, 4, 10}, glm::vec3{0, -4, 10}};
        std::array<nw::nav::NavRayProjectionInput, 4> rays;
        std::array<nw::nav::NavRayProjectionResult, 4> hits;
        for (size_t i = 0; i < rays.size(); ++i) {
            rays[i] = {.origin = start_position + offsets[i], .displacement = {0, 0, -20}};
        }
        nw::toolset::project_toolset_preview_rays(session, rays, hits);
        const auto hit = std::ranges::find_if(hits, [](const auto& row) { return row.status == nw::nav::NavStatus::ok; });
        ASSERT_NE(hit, hits.end());
        item_position = hit->position;
        ASSERT_GT(glm::distance(start_position, item_position), 3.0f);
        ASSERT_TRUE(components.set_position(item, item_position));
    }

    void TearDown() override
    {
        nw::toolset::stop_toolset_preview(session);
        if (fixture_rig) { nw::kernel::models().release("pmh0"); }
    }

    nw::toolset::PreviewTickStats tick(nw::toolset::PreviewInputSample sample = {})
    {
        std::array<nw::toolset::PreviewActorLocomotion, 1> locomotion;
        const auto result = nw::toolset::tick_toolset_preview(session, {&sample, 1}, {&spatial, 1}, locomotion);
        EXPECT_EQ(result.status, nw::toolset::PreviewStatus::ok);
        return result;
    }

    void start_pickup()
    {
        nw::toolset::PreviewInputSample sample;
        ASSERT_TRUE(nw::toolset::set_preview_item_action(sample, {item}));
        const auto result = tick(sample);
        ASSERT_EQ(result.path_request_count, 1u);
        ASSERT_EQ(nw::toolset::toolset_preview_action_state(session).phase, nw::gameplay::ActionPhase::moving);
        ASSERT_EQ(result.action_result.status, nw::gameplay::ActionStatus::accepted);
    }

    void expect_cancelled(nw::toolset::PreviewInputSample sample)
    {
        const auto result = tick(sample);
        ASSERT_EQ(result.action_result.status, nw::gameplay::ActionStatus::cancelled);
        const auto stopped_position = spatial.position;
        for (int i = 0; i < 180; ++i) {
            EXPECT_EQ(tick().action_result.status, nw::gameplay::ActionStatus::none);
        }
        EXPECT_EQ(spatial.position, stopped_position);
        EXPECT_EQ(nw::kernel::objects().components().find_spatial(item)->area,
            nw::toolset::toolset_preview_item_world(session).area.id);
    }
};

TEST_F(ClientPickupActions, ApproachesAndPicksUpThroughTheExistingMover)
{
    start_pickup();
    size_t transfers = 0;
    for (int i = 0; i < 1200; ++i) {
        const auto result = tick();
        transfers += static_cast<size_t>(result.action_result.changed);
        if (nw::toolset::toolset_preview_action_state(session).phase == nw::gameplay::ActionPhase::finished) { break; }
    }
    const auto action = nw::toolset::toolset_preview_action_state(session);
    ASSERT_EQ(action.status, nw::gameplay::ActionStatus::applied);
    EXPECT_EQ(transfers, 1u);
    EXPECT_GT(glm::distance(spatial.position, start_position), 1.0f);
    EXPECT_LE(glm::distance(spatial.position, item_position), 1.0f);
    EXPECT_TRUE(nw::kernel::objects().get<nw::Creature>(session.actor())->inventory().has_item(nw::kernel::objects().get<nw::Item>(item)));
    for (int i = 0; i < 10; ++i) {
        EXPECT_FALSE(tick().action_result.changed);
    }
}

TEST_F(ClientPickupActions, KeyboardMovementCancelsPickupAndItsRoute)
{
    start_pickup();
    nw::toolset::PcDeviceSample input;
    input.keys[static_cast<size_t>(nw::toolset::PcKey::w)] = true;
    nw::toolset::PreviewInputSample sample;
    ASSERT_EQ(nw::toolset::translate_pc_input_samples({&input, 1}, {&sample, 1}), nw::toolset::PreviewStatus::ok);
    expect_cancelled(sample);
}

TEST_F(ClientPickupActions, ControllerDriftKeepsApproachingAndIntentionalMovementCancels)
{
    start_pickup();
    nw::toolset::PcDeviceSample input;
    input.controller_connected = true;
    input.left_stick.y = -0.1f;
    nw::toolset::PreviewInputSample sample;
    ASSERT_EQ(nw::toolset::translate_pc_input_samples({&input, 1}, {&sample, 1}), nw::toolset::PreviewStatus::ok);
    EXPECT_EQ(tick(sample).action_result.status, nw::gameplay::ActionStatus::none);
    EXPECT_EQ(nw::toolset::toolset_preview_action_state(session).phase, nw::gameplay::ActionPhase::moving);
    input.left_stick.y = -0.75f;
    ASSERT_EQ(nw::toolset::translate_pc_input_samples({&input, 1}, {&sample, 1}), nw::toolset::PreviewStatus::ok);
    expect_cancelled(sample);
}

TEST_F(ClientPickupActions, NewClickReplacesPickupWithoutLeavingADeferredTransfer)
{
    start_pickup();
    nw::toolset::PreviewInputSample sample;
    ASSERT_TRUE(nw::toolset::set_preview_click_target(sample, start_position));
    tick(sample);
    for (int i = 0; i < 180; ++i) {
        tick();
    }
    EXPECT_EQ(nw::toolset::toolset_preview_action_state(session).kind, nw::gameplay::ActionKind::move_to);
    EXPECT_EQ(nw::toolset::toolset_preview_action_state(session).status, nw::gameplay::ActionStatus::applied);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(item)->area,
        nw::toolset::toolset_preview_item_world(session).area.id);
}

TEST_F(ClientPickupActions, UnreachableItemFinishesWithoutOwnershipChanges)
{
    ASSERT_TRUE(nw::kernel::objects().components().set_position(item, item_position + glm::vec3{0, 0, 3}));
    start_pickup();
    for (int i = 0; i < 1200; ++i) {
        tick();
        if (nw::toolset::toolset_preview_action_state(session).phase == nw::gameplay::ActionPhase::finished) { break; }
    }
    const auto action = nw::toolset::toolset_preview_action_state(session);
    EXPECT_EQ(action.phase, nw::gameplay::ActionPhase::finished);
    EXPECT_NE(action.status, nw::gameplay::ActionStatus::applied);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(item)->area,
        nw::toolset::toolset_preview_item_world(session).area.id);
}

} // namespace

TEST(ClientPreview, ItemCommandIsAnEdgeAndReplacesOtherPointerActions)
{
    using namespace nw::toolset;
    const nw::ObjectHandle item{.id = static_cast<nw::ObjectID>(123), .type = nw::ObjectType::item, .version = 1};
    PreviewInputSample sample;
    ASSERT_TRUE(set_preview_click_target(sample, {1, 2, 0}));
    ASSERT_TRUE(set_preview_item_action(sample, {item}));
    EXPECT_EQ(sample.flags, preview_input_item_action);
    PreviewFixedStepState fixed;
    std::array<PreviewInputSample, 6> outputs;
    const auto result = build_preview_tick_samples(fixed, 0.05, sample, outputs);
    ASSERT_EQ(result.tick_count, 3u);
    EXPECT_EQ(outputs[0].flags, preview_input_item_action);
    EXPECT_EQ(outputs[0].item_action.item, item);
    EXPECT_EQ(outputs[1].flags, preview_input_none);
    EXPECT_EQ(outputs[2].flags, preview_input_none);
    ASSERT_TRUE(set_preview_click_target(sample, {2, 3, 0}));
    EXPECT_EQ(sample.flags, preview_input_click_target);
}

TEST_F(ClientPickupActions, GrabStopsMovementAndCanBeCancelledBeforeCommit)
{
    using namespace nw::toolset;
    using nw::gameplay::ActionPhase;
    EXPECT_EQ(toolset_preview_action_state(session).pickup_ticks, 30u);
    start_pickup();
    for (int i = 0; i < 1200 && toolset_preview_action_state(session).phase == ActionPhase::moving; ++i) {
        tick();
    }
    ASSERT_EQ(toolset_preview_action_state(session).phase, ActionPhase::grabbing);
    const auto stopped = spatial.position;
    for (int i = 0; i < 15; ++i) {
        EXPECT_FALSE(tick().action_result.changed);
        EXPECT_EQ(spatial.position, stopped);
        EXPECT_EQ(spatial.velocity, glm::vec3(0.0f));
    }
    PreviewInputSample cancel;
    cancel.flags = preview_input_cancel;
    expect_cancelled(cancel);
}

TEST_F(ClientPickupActions, LiveInputFramesDoNotRestartOrSlideTheGrab)
{
    using namespace nw::toolset;
    using nw::gameplay::ActionPhase;
    RuntimeInputState input;
    PreviewFixedStepState fixed;
    ASSERT_TRUE(set_preview_item_action(input.pending, {item}));
    std::array<PreviewInputSample, 6> samples;
    std::array<PreviewActorLocomotion, 1> locomotion;
    constexpr std::array frame_times{0.001, 0.016, 0.05, 0.008, 0.03};
    size_t transfers = 0;
    size_t route_requests = 0;
    bool saw_grab = false;
    bool advanced_grab = false;
    bool slid = false;
    glm::vec3 stopped_position{};
    for (size_t frame = 0; frame < 300; ++frame) {
        const auto dt = frame_times[frame % frame_times.size()];
        PcDeviceSample physical;
        ASSERT_EQ(acquire_pc_device_sample(input, dt,
                      {.keyboard = false, .controller = false, .pointer = true}, physical),
            PreviewStatus::ok);
        PreviewInputSample sample;
        ASSERT_EQ(translate_pc_input_samples({&physical, 1}, {&sample, 1}), PreviewStatus::ok);
        const auto batch = build_preview_tick_samples(fixed, dt, sample, samples);
        ASSERT_EQ(batch.status, PreviewStatus::ok);
        if (batch.tick_count == 0) { continue; }
        const auto result = tick_toolset_preview(session, std::span{samples}.first(batch.tick_count), {&spatial, 1}, locomotion);
        ASSERT_EQ(result.status, PreviewStatus::ok);
        consume_runtime_input_edges(input);
        transfers += result.action_result.changed;
        route_requests += result.path_request_count;
        const auto action = toolset_preview_action_state(session);
        if (action.phase == ActionPhase::grabbing || action.phase == ActionPhase::recovering) {
            if (!saw_grab) { stopped_position = spatial.position; }
            saw_grab = true;
            slid |= spatial.position != stopped_position;
            advanced_grab |= action.elapsed_ticks > 0;
        }
        if (action.phase == ActionPhase::finished) { break; }
    }
    EXPECT_TRUE(saw_grab);
    EXPECT_TRUE(advanced_grab);
    EXPECT_FALSE(slid);
    EXPECT_EQ(route_requests, 1u);
    EXPECT_EQ(transfers, 1u);
    EXPECT_EQ(toolset_preview_action_state(session).phase, ActionPhase::finished);
    EXPECT_EQ(toolset_preview_action_state(session).status, nw::gameplay::ActionStatus::applied);
    EXPECT_TRUE(nw::kernel::objects().get<nw::Creature>(session.actor())->inventory().has_item(nw::kernel::objects().get<nw::Item>(item)));
}
