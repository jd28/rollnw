#include "preview_session.hpp"

#include "object_edits.hpp"

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/nav/NavBlockers.hpp>
#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavWorld.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace nw::toolset {
namespace {

constexpr double fixed_step_seconds = 1.0 / 60.0;
constexpr double maximum_frame_seconds = 0.1;
constexpr size_t maximum_catchup_ticks = 6;
constexpr float fallback_walk_rate = 2.0f;
constexpr float preview_clearance_padding = 0.1f;
constexpr float route_corner_epsilon = 0.05f;
constexpr float movement_axis_epsilon = 1.0e-5f;
constexpr size_t maximum_route_motion_attempts = 2;
constexpr float turn_radians_per_second = 2.5f;
constexpr float look_radians_per_second = 2.5f;
constexpr float zoom_units_per_second = 8.0f;
constexpr float zoom_units_per_wheel_step = 1.25f;
constexpr float minimum_camera_pitch = -1.45f;
constexpr float maximum_camera_pitch = -0.05f;
constexpr float minimum_camera_distance = 2.0f;
constexpr float maximum_camera_distance = 40.0f;
constexpr uint32_t edge_input_flags = preview_input_click_target | preview_input_cancel;

bool finite(const glm::vec2& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float horizontal_distance_squared(const glm::vec3& lhs, const glm::vec3& rhs)
{
    const glm::vec2 delta{lhs.x - rhs.x, lhs.y - rhs.y};
    return glm::dot(delta, delta);
}

bool valid_camera(const PreviewCameraState& camera)
{
    return finite(camera.focus)
        && std::isfinite(camera.yaw)
        && std::isfinite(camera.pitch)
        && std::isfinite(camera.distance)
        && camera.distance > 0.0f;
}

int32_t movement_rate_row(StringView value)
{
    constexpr std::array<StringView, 9> names{
        "PLAYER",
        "NOMOVE",
        "VSLOW",
        "SLOW",
        "NORM",
        "FAST",
        "VFAST",
        "DEFAULT",
        "DFAST",
    };
    for (size_t index = 0; index < names.size(); ++index) {
        if (string::icmp(value, names[index])) return static_cast<int32_t>(index);
    }
    if (const auto numeric = string::from<int32_t>(value)) return *numeric;
    return -1;
}

float valid_speed_row(const StaticTwoDA* speeds, int32_t row)
{
    if (!speeds || row < 0 || static_cast<size_t>(row) >= speeds->rows()) return -1.0f;
    float rate = -1.0f;
    if (!speeds->get_to(static_cast<size_t>(row), "WALKRATE", rate, false)
        || !std::isfinite(rate)
        || rate < 0.0f) {
        return -1.0f;
    }
    return rate;
}

float resolve_walk_rate(ObjectHandle actor, bool& fallback, bool& disabled)
{
    fallback = false;
    disabled = false;
    const auto& components = kernel::objects().components();
    const auto* visual = components.find_visual(actor);
    const auto* appearances = kernel::twodas().get("appearance");
    const auto* speeds = kernel::twodas().get("creaturespeed");

    int32_t speed_row = -1;
    if (visual && appearances && visual->appearance >= 0
        && static_cast<size_t>(visual->appearance) < appearances->rows()) {
        StringView value;
        if (appearances->get_to(
                static_cast<size_t>(visual->appearance), "MOVERATE", value, false)) {
            speed_row = movement_rate_row(value);
        }
    }

    if (speed_row == 1) {
        disabled = true;
        return 0.0f;
    }
    if (const float rate = valid_speed_row(speeds, speed_row); rate > 0.0f) return rate;

    fallback = true;
    if (const float player_rate = valid_speed_row(speeds, 0); player_rate > 0.0f) {
        return player_rate;
    }
    return fallback_walk_rate;
}

float resolve_actor_clearance(ObjectHandle actor)
{
    const auto* visual = kernel::objects().components().find_visual(actor);
    const auto* appearances = kernel::twodas().get("appearance");
    if (!visual || !appearances || visual->appearance < 0
        || static_cast<size_t>(visual->appearance) >= appearances->rows()) {
        return -1.0f;
    }

    float clearance = -1.0f;
    if (!appearances->get_to(
            static_cast<size_t>(visual->appearance), "PERSPACE", clearance, false)
        || !std::isfinite(clearance)
        || clearance < 0.0f) {
        return -1.0f;
    }
    return clearance + preview_clearance_padding;
}

glm::vec2 clamped_movement_axis(glm::vec2 axis)
{
    axis = glm::clamp(axis, glm::vec2{-1.0f}, glm::vec2{1.0f});
    const float length_squared = glm::dot(axis, axis);
    return length_squared > 1.0f ? axis / std::sqrt(length_squared) : axis;
}

glm::vec3 horizontal_facing(glm::vec3 facing)
{
    facing.z = 0.0f;
    const float facing_length_squared = glm::dot(facing, facing);
    return facing_length_squared > movement_axis_epsilon
        ? facing / std::sqrt(facing_length_squared)
        : glm::vec3{1.0f, 0.0f, 0.0f};
}

glm::vec3 rotate_facing(glm::vec3 facing, float radians)
{
    facing = horizontal_facing(facing);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        facing.x * cosine - facing.y * sine,
        facing.x * sine + facing.y * cosine,
        0.0f,
    };
}

PreviewActorLocomotion direct_locomotion(glm::vec2 movement_axis, float turn_axis)
{
    if (std::abs(movement_axis.y) >= std::abs(movement_axis.x)
        && std::abs(movement_axis.y) > movement_axis_epsilon) {
        return movement_axis.y > 0.0f
            ? PreviewActorLocomotion::walking_forward
            : PreviewActorLocomotion::walking_backward;
    }
    if (std::abs(movement_axis.x) > movement_axis_epsilon) {
        return movement_axis.x > 0.0f
            ? PreviewActorLocomotion::strafing_right
            : PreviewActorLocomotion::strafing_left;
    }
    if (std::abs(turn_axis) > movement_axis_epsilon) {
        return turn_axis > 0.0f
            ? PreviewActorLocomotion::turning_right
            : PreviewActorLocomotion::turning_left;
    }
    return PreviewActorLocomotion::idle;
}

} // namespace

struct ToolsetPreviewSession::Impl {
    // The F9 v1 contract has exactly one locally controlled test actor. Keep
    // that true singleton scalar; navigation and renderer boundaries retain
    // their batch paths for the many-agent systems that actually own arrays.
    nav::NavWorldState nav_world;
    Vector<glm::vec3> route_corners;
    ObjectHandle area{};
    ObjectHandle actor{};
    uint32_t nav_agent = UINT32_MAX;
    uint32_t route_corner = 0;
    ObjectSpatialState spatial;
    PreviewCameraState camera;
    PreviewActorLocomotion locomotion = PreviewActorLocomotion::idle;
    float walk_rate = 0.0f;
    float clearance = 0.0f;
    bool active = false;

    ~Impl()
    {
        if (nav_agent != UINT32_MAX) {
            const std::array agents{nav_agent};
            nav::release_nav_agents(nav_world, agents);
        }
        if (actor.type != ObjectType::invalid && kernel::objects().valid(actor)) {
            kernel::objects().destroy(actor);
        }
    }

    void clear() noexcept
    {
        if (nav_agent != UINT32_MAX) {
            const std::array agents{nav_agent};
            nav::release_nav_agents(nav_world, agents);
        }
        if (actor.type != ObjectType::invalid && kernel::objects().valid(actor)) {
            kernel::objects().destroy(actor);
        }
        route_corners.clear();
        area = ObjectHandle{};
        actor = ObjectHandle{};
        nav_agent = UINT32_MAX;
        route_corner = 0;
        spatial = ObjectSpatialState{};
        camera = PreviewCameraState{};
        locomotion = PreviewActorLocomotion::idle;
        walk_rate = 0.0f;
        clearance = 0.0f;
        active = false;
    }
};

ToolsetPreviewSession::ToolsetPreviewSession()
    : impl_{std::make_unique<Impl>()}
{
}

ToolsetPreviewSession::~ToolsetPreviewSession()
{
    stop_toolset_preview(*this);
}

ToolsetPreviewSession::ToolsetPreviewSession(ToolsetPreviewSession&&) noexcept = default;
ToolsetPreviewSession& ToolsetPreviewSession::operator=(ToolsetPreviewSession&&) noexcept = default;

bool ToolsetPreviewSession::active() const noexcept
{
    return impl_ && impl_->active;
}

ObjectHandle ToolsetPreviewSession::actor() const noexcept
{
    return impl_ ? impl_->actor : ObjectHandle{};
}

const PreviewCameraState& ToolsetPreviewSession::camera() const noexcept
{
    static const PreviewCameraState empty;
    return impl_ ? impl_->camera : empty;
}

PreviewFixedStepStats build_preview_tick_samples(
    PreviewFixedStepState& state,
    double elapsed_seconds,
    const PreviewInputSample& frame_sample,
    std::span<PreviewInputSample> output)
{
    PreviewFixedStepStats stats;
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0
        || !finite(frame_sample.move_axis)
        || !std::isfinite(frame_sample.turn_axis)
        || !finite(frame_sample.look_axis)
        || !std::isfinite(frame_sample.zoom_axis)
        || !std::isfinite(frame_sample.zoom_delta)
        || ((frame_sample.flags & preview_input_click_target) != 0
            && !finite(frame_sample.click_target))) {
        stats.status = PreviewStatus::invalid_input;
        return stats;
    }

    const double accumulator = state.accumulator + std::min(elapsed_seconds, maximum_frame_seconds);
    const size_t available_ticks = static_cast<size_t>(accumulator / fixed_step_seconds);
    const size_t consumed_ticks = std::min(available_ticks, maximum_catchup_ticks);
    if (output.size() < consumed_ticks) {
        stats.status = PreviewStatus::output_full;
        return stats;
    }
    const size_t emitted_ticks = consumed_ticks;

    for (size_t index = 0; index < emitted_ticks; ++index) {
        output[index] = frame_sample;
        if (index > 0) {
            output[index].flags &= ~edge_input_flags;
            output[index].zoom_delta = 0.0f;
        }
    }

    double remainder = accumulator - static_cast<double>(consumed_ticks) * fixed_step_seconds;
    if (available_ticks > maximum_catchup_ticks) {
        stats.dropped_tick_count = available_ticks - maximum_catchup_ticks;
        remainder -= static_cast<double>(stats.dropped_tick_count) * fixed_step_seconds;
    }
    state.accumulator = std::max(0.0, remainder);
    stats.tick_count = emitted_ticks;
    return stats;
}

glm::vec2 preview_radial_deadzone(glm::vec2 axis, float deadzone) noexcept
{
    if (!finite(axis) || !std::isfinite(deadzone)
        || deadzone < 0.0f || deadzone >= 1.0f) {
        return {};
    }

    const float magnitude_squared = glm::dot(axis, axis);
    if (magnitude_squared <= deadzone * deadzone) return {};

    const float magnitude = std::sqrt(magnitude_squared);
    const float clamped_magnitude = std::min(magnitude, 1.0f);
    const float scaled_magnitude = (clamped_magnitude - deadzone) / (1.0f - deadzone);
    return axis * (scaled_magnitude / magnitude);
}

void stop_toolset_preview(ToolsetPreviewSession& session) noexcept
{
    if (!session.impl_) return;
    session.impl_->clear();
}

PreviewSessionStartResult start_toolset_preview(
    ToolsetPreviewSession& session,
    const PreviewSessionStartInput& input)
{
    if (!session.impl_) session.impl_ = std::make_unique<ToolsetPreviewSession::Impl>();
    stop_toolset_preview(session);
    PreviewSessionStartResult result;

    auto* area = input.area.type == ObjectType::area
        ? kernel::objects().get<Area>(input.area)
        : nullptr;
    const bool spawn_position_valid = input.spawn_source
            == PreviewSessionStartInput::SpawnSource::position
        && finite(input.spawn_position);
    const bool spawn_ray_valid = input.spawn_source
            == PreviewSessionStartInput::SpawnSource::navigation_ray
        && finite(input.spawn_ray.origin)
        && finite(input.spawn_ray.displacement)
        && glm::dot(input.spawn_ray.displacement, input.spawn_ray.displacement) > 1.0e-12f;
    if (!area || area->width <= 0 || area->height <= 0
        || input.actor.type != ResourceType::utc
        || !input.actor.valid()
        || !kernel::resman().contains(input.actor)
        || (!spawn_position_valid && !spawn_ray_valid)
        || !valid_camera(input.camera)) {
        result.diagnostic = "Preview area, actor, spawn target, or camera is invalid";
        return result;
    }

    nav::NavGeometry base_geometry;
    const auto tile_stats = nav::build_area_tile_nav_geometry(
        *area, kernel::resman(), base_geometry);
    result.stats.tile_count = tile_stats.tile_count;
    result.stats.navigation_triangle_count = base_geometry.triangle_count();
    if (!base_geometry.valid() || base_geometry.triangle_count() == 0) {
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Area has no usable NWN tile walkmesh geometry";
        return result;
    }

    const auto* surfaces = kernel::twodas().get("surfacemat");
    Vector<uint8_t> walkable;
    if (!surfaces || nav::build_nav_surface_walkability(*surfaces, walkable).walkable_count == 0) {
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "NWN surface walkability data is unavailable";
        return result;
    }

    nav::NavBuildStats nav_stats;
    if (nav::build_nav_world(base_geometry, walkable,
            static_cast<uint32_t>(area->width), static_cast<uint32_t>(area->height),
            session.impl_->nav_world, nav_stats)
        != nav::NavStatus::ok) {
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Area navigation mesh construction failed";
        return result;
    }
    result.stats.navigation_payload_bytes = nav_stats.payload_bytes;
    result.stats.portal_edge_count = nav_stats.portal_edge_count;

    nav::NavGeometry blocker_geometry;
    nav::build_area_object_nav_geometry(*area, kernel::resman(), blocker_geometry);
    result.stats.blocker_triangle_count = blocker_geometry.triangle_count();
    Vector<nav::NavBlockerOverlapInput> overlaps;
    nav::NavBlockerOverlapStats overlap_stats;
    if (nav::build_nav_blocker_overlaps(base_geometry, walkable, blocker_geometry,
            static_cast<uint32_t>(area->width), static_cast<uint32_t>(area->height),
            overlaps, overlap_stats)
        != nav::NavStatus::ok) {
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Area blocker overlap construction failed";
        return result;
    }
    nav::configure_nav_blockers(session.impl_->nav_world, overlaps);
    result.stats.blocker_overlap_count = overlaps.size();

    glm::vec3 spawn_position = input.spawn_position;
    if (input.spawn_source == PreviewSessionStartInput::SpawnSource::navigation_ray) {
        std::array<nav::NavRayProjectionResult, 1> projected{};
        nav::project_nav_rays(session.impl_->nav_world,
            std::span<const nav::NavRayProjectionInput>{&input.spawn_ray, 1},
            projected);
        if (projected[0].status != nav::NavStatus::ok) {
            result.status = PreviewStatus::navigation_failed;
            result.diagnostic = "Preview placement ray did not hit walkable navigation geometry";
            return result;
        }
        spawn_position = projected[0].position;
    }

    const AreaObjectBlueprintPlacement placement{
        .resource = input.actor,
        .transform = {
            .position = spawn_position,
            .orientation = {1.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
        },
    };
    auto loaded = load_area_object_blueprints(input.area,
        std::span<const AreaObjectBlueprintPlacement>{&placement, 1});
    if (!loaded.ok() || loaded.objects.size() != 1) {
        result.status = PreviewStatus::actor_failed;
        result.diagnostic = loaded.diagnostic.empty()
            ? "Preview actor instantiation failed"
            : std::move(loaded.diagnostic);
        return result;
    }

    auto& state = *session.impl_;
    state.area = input.area;
    state.actor = loaded.objects[0];
    state.clearance = resolve_actor_clearance(state.actor);
    if (state.clearance < 0.0f) {
        kernel::objects().destroy(state.actor);
        state.actor = ObjectHandle{};
        result.status = PreviewStatus::actor_failed;
        result.diagnostic = "Preview actor appearance has no valid PERSPACE clearance";
        return result;
    }
    result.stats.actor_clearance = state.clearance;
    const std::array registrations{
        nav::NavAgentRegistrationInput{spawn_position, state.clearance},
    };
    std::array<nav::NavAgentRegistrationResult, 1> registered{};
    nav::register_nav_agents(state.nav_world, registrations, registered);
    if (registered[0].status != nav::NavStatus::ok
        && registered[0].status != nav::NavStatus::clamped) {
        kernel::objects().destroy(state.actor);
        state.actor = ObjectHandle{};
        result.status = PreviewStatus::actor_failed;
        result.diagnostic = "Preview actor could not be placed on reachable geometry";
        return result;
    }

    state.nav_agent = registered[0].agent;
    auto* spatial = kernel::objects().components().find_spatial(state.actor);
    if (!spatial
        || !kernel::objects().components().set_position(state.actor, registered[0].position)) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::actor_failed;
        result.diagnostic = "Preview actor spatial initialization failed";
        return result;
    }
    spatial = kernel::objects().components().find_spatial(state.actor);
    state.spatial = *spatial;
    state.camera = input.camera;
    state.camera.focus = state.spatial.position;
    state.camera.pitch = std::clamp(state.camera.pitch, minimum_camera_pitch, maximum_camera_pitch);
    state.camera.distance = std::clamp(
        state.camera.distance, minimum_camera_distance, maximum_camera_distance);
    state.walk_rate = resolve_walk_rate(
        state.actor, result.stats.walk_rate_fallback, result.stats.movement_disabled);
    result.stats.walk_rate = state.walk_rate;
    state.active = true;

    result.status = PreviewStatus::ok;
    result.actor = state.actor;
    return result;
}

nav::NavBatchStats project_toolset_preview_rays(
    const ToolsetPreviewSession& session,
    std::span<const nav::NavRayProjectionInput> inputs,
    std::span<nav::NavRayProjectionResult> results)
{
    if (!session.impl_ || !session.impl_->active) {
        nav::NavBatchStats stats;
        stats.input_count = inputs.size();
        stats.rejected_count = inputs.size();
        return stats;
    }
    return nav::project_nav_rays(session.impl_->nav_world, inputs, results);
}

PreviewTickStats tick_toolset_preview(
    ToolsetPreviewSession& session,
    std::span<const PreviewInputSample> samples,
    std::span<ObjectSpatialState> spatial_output,
    std::span<PreviewActorLocomotion> locomotion_output)
{
    PreviewTickStats stats;
    if (!session.impl_ || !session.impl_->active) return stats;
    auto& state = *session.impl_;
    if (spatial_output.empty() || locomotion_output.empty()) {
        stats.status = PreviewStatus::output_full;
        return stats;
    }

    stats.status = PreviewStatus::ok;
    stats.sample_count = samples.size();
    for (const auto& sample : samples) {
        if (!finite(sample.move_axis) || !finite(sample.look_axis)
            || !std::isfinite(sample.turn_axis)
            || !std::isfinite(sample.zoom_axis)
            || !std::isfinite(sample.zoom_delta)
            || ((sample.flags & preview_input_click_target) != 0 && !finite(sample.click_target))) {
            stats.status = PreviewStatus::invalid_input;
            continue;
        }

        state.camera.yaw += sample.look_axis.x * look_radians_per_second
            * static_cast<float>(fixed_step_seconds);
        state.camera.pitch = std::clamp(
            state.camera.pitch + sample.look_axis.y * look_radians_per_second * static_cast<float>(fixed_step_seconds),
            minimum_camera_pitch, maximum_camera_pitch);
        state.camera.distance = std::clamp(
            state.camera.distance
                - sample.zoom_axis * zoom_units_per_second * static_cast<float>(fixed_step_seconds)
                - sample.zoom_delta * zoom_units_per_wheel_step,
            minimum_camera_distance, maximum_camera_distance);

        if ((sample.flags & preview_input_cancel) != 0) {
            state.route_corners.clear();
            state.route_corner = 0;
        }
        if ((sample.flags & preview_input_click_target) != 0) {
            ++stats.path_request_count;
            const std::array request{
                nav::NavPathRequest{state.spatial.position, sample.click_target, state.clearance},
            };
            std::array<nav::NavPathResult, 1> path_result{};
            nav::find_nav_paths(state.nav_world, request, state.route_corners, path_result);
            if (path_result[0].status == nav::NavStatus::ok
                || path_result[0].status == nav::NavStatus::clamped) {
                state.route_corner = path_result[0].corner_offset;
                const uint32_t end = path_result[0].corner_offset + path_result[0].corner_count;
                while (state.route_corner < end) {
                    if (horizontal_distance_squared(state.spatial.position,
                            state.route_corners[state.route_corner])
                        > route_corner_epsilon * route_corner_epsilon) {
                        break;
                    }
                    ++state.route_corner;
                }
            } else {
                state.route_corners.clear();
                state.route_corner = 0;
                ++stats.path_failure_count;
            }
        }

        const glm::vec2 movement_axis = clamped_movement_axis(sample.move_axis);
        const float turn_axis = std::clamp(sample.turn_axis, -1.0f, 1.0f);
        const bool direct_movement = std::abs(movement_axis.x) > movement_axis_epsilon
            || std::abs(movement_axis.y) > movement_axis_epsilon;
        const bool direct_input = direct_movement
            || std::abs(turn_axis) > movement_axis_epsilon;

        if (std::abs(turn_axis) > movement_axis_epsilon) {
            state.spatial.orientation = rotate_facing(
                state.spatial.orientation,
                -turn_axis * turn_radians_per_second
                    * static_cast<float>(fixed_step_seconds));
        }

        glm::vec3 desired{0.0f};
        const auto route_displacement = [&state]() {
            while (state.route_corner < state.route_corners.size()) {
                if (horizontal_distance_squared(state.spatial.position,
                        state.route_corners[state.route_corner])
                    > route_corner_epsilon * route_corner_epsilon) {
                    break;
                }
                ++state.route_corner;
            }
            if (state.route_corner >= state.route_corners.size()) return glm::vec3{};

            glm::vec3 remaining = state.route_corners[state.route_corner] - state.spatial.position;
            remaining.z = 0.0f;
            const float distance = glm::length(glm::vec2{remaining.x, remaining.y});
            if (distance <= route_corner_epsilon) return glm::vec3{};
            return remaining / distance
                * std::min(distance, state.walk_rate * static_cast<float>(fixed_step_seconds));
        };
        if (direct_input) {
            state.route_corners.clear();
            state.route_corner = 0;
        }
        if (direct_movement) {
            const glm::vec3 forward = horizontal_facing(state.spatial.orientation);
            const glm::vec3 right{forward.y, -forward.x, 0.0f};
            desired = (forward * movement_axis.y + right * movement_axis.x)
                * state.walk_rate * static_cast<float>(fixed_step_seconds);
        } else if (state.route_corner < state.route_corners.size()) {
            desired = route_displacement();
        }

        if (direct_input) {
            state.locomotion = direct_locomotion(movement_axis, turn_axis);
        } else if (glm::dot(desired, desired) > movement_axis_epsilon) {
            state.locomotion = PreviewActorLocomotion::walking_forward;
        } else {
            state.locomotion = PreviewActorLocomotion::idle;
        }

        state.spatial.velocity = {0.0f, 0.0f, 0.0f};
        if (glm::dot(desired, desired) > movement_axis_epsilon) {
            const size_t attempt_count = direct_movement ? 1 : maximum_route_motion_attempts;
            for (size_t attempt = 0; attempt < attempt_count; ++attempt) {
                const std::array motion{nav::NavAgentMotionInput{
                    state.nav_agent, state.spatial.position, desired}};
                std::array<nav::NavAgentMotionResult, 1> moved{};
                nav::move_nav_agents(state.nav_world, motion, moved);
                if (moved[0].status != nav::NavStatus::ok
                    && moved[0].status != nav::NavStatus::clamped) {
                    ++stats.blocked_count;
                    if (!direct_movement) {
                        state.route_corners.clear();
                        state.route_corner = 0;
                        state.locomotion = PreviewActorLocomotion::idle;
                    }
                    break;
                }

                state.spatial.position = moved[0].position;
                state.spatial.velocity = moved[0].applied_displacement
                    / static_cast<float>(fixed_step_seconds);
                glm::vec3 horizontal_displacement = moved[0].applied_displacement;
                horizontal_displacement.z = 0.0f;
                const float horizontal_displacement_squared = glm::dot(horizontal_displacement, horizontal_displacement);
                if (horizontal_displacement_squared > movement_axis_epsilon) {
                    if (!direct_input) {
                        state.spatial.orientation = glm::normalize(horizontal_displacement);
                    }
                    ++stats.movement_count;
                    break;
                }

                if (!direct_movement
                    && state.route_corner + 1 < state.route_corners.size()) {
                    ++state.route_corner;
                    desired = route_displacement();
                    if (glm::dot(desired, desired) > movement_axis_epsilon) continue;
                }

                ++stats.blocked_count;
                if (!direct_movement) {
                    state.route_corners.clear();
                    state.route_corner = 0;
                    state.locomotion = PreviewActorLocomotion::idle;
                }
                break;
            }
        }

        auto& components = kernel::objects().components();
        if (!components.set_position(state.actor, state.spatial.position)
            || !components.set_orientation(state.actor, state.spatial.orientation)
            || !components.set_velocity(state.actor, state.spatial.velocity)) {
            stats.status = PreviewStatus::actor_failed;
            break;
        }
        state.camera.focus = state.spatial.position;
    }

    spatial_output[0] = state.spatial;
    locomotion_output[0] = state.locomotion;
    stats.output_count = 1;
    return stats;
}

} // namespace nw::toolset
