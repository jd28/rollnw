#include "preview_session.hpp"

#include "object_edits.hpp"

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavTileBuild.hpp>
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
constexpr uint32_t edge_input_flags = preview_input_click_target
    | preview_input_cancel | preview_input_click_door;

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

bool overlaps_expanded_bounds_xy(
    const glm::vec3& point,
    const glm::vec3& minimum,
    const glm::vec3& maximum,
    float radius) noexcept
{
    return finite(point) && finite(minimum) && finite(maximum)
        && std::isfinite(radius) && radius >= 0.0f
        && minimum.x <= maximum.x && minimum.y <= maximum.y
        && point.x >= minimum.x - radius
        && point.x <= maximum.x + radius
        && point.y >= minimum.y - radius
        && point.y <= maximum.y + radius;
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

bool build_projected_door_links(nav::NavAreaBuildSource& source,
    Vector<nav::NavDoorObstacleRow>& doors,
    std::span<const uint8_t> authored_active,
    const nav::NavTileBuildConfig& config)
{
    source.door_links.clear();
    if (doors.empty()) return true;

    Vector<uint8_t> projection_active{
        authored_active.begin(), authored_active.end()};
    Vector<nav::NavAgentRegistrationInput> projection_inputs;
    Vector<uint32_t> projection_doors;
    projection_inputs.reserve(doors.size() * 2);
    projection_doors.reserve(doors.size());
    const float eroded_radius
        = static_cast<float>(config.erosion_cells) * config.cell_size;
    for (size_t door_index = 0; door_index < doors.size(); ++door_index) {
        auto& door = doors[door_index];
        if (door.closed_obstacle_state >= projection_active.size()
            || door.open1_obstacle_state >= projection_active.size()
            || door.open2_obstacle_state >= projection_active.size()
            || !finite(door.position) || !finite(door.normal)
            || !std::isfinite(door.closed_half_depth)
            || door.closed_half_depth <= 0.0f) {
            continue;
        }
        projection_active[door.closed_obstacle_state] = 0;
        projection_active[door.open1_obstacle_state] = 0;
        projection_active[door.open2_obstacle_state] = 0;
        const float offset
            = door.closed_half_depth + eroded_radius + config.cell_size;
        projection_inputs.push_back(
            {door.position - door.normal * offset});
        projection_inputs.push_back(
            {door.position + door.normal * offset});
        projection_doors.push_back(static_cast<uint32_t>(door_index));
    }
    if (projection_inputs.empty()) return true;

    nav::NavWorldState projection_world;
    nav::NavTiledWorldBuildStats build_stats;
    if (nav::build_tiled_nav_world(source, projection_active, config,
            projection_world, build_stats)
        != nav::NavStatus::ok) {
        return false;
    }
    Vector<nav::NavAgentRegistrationResult> projected(
        projection_inputs.size());
    nav::register_nav_agents(
        projection_world, projection_inputs, projected);
    source.door_links.reserve(projection_doors.size() * 2);
    for (size_t row = 0; row < projection_doors.size(); ++row) {
        auto& door = doors[projection_doors[row]];
        const auto& side0 = projected[row * 2];
        const auto& side1 = projected[row * 2 + 1];
        door.approach_valid[0] = static_cast<uint8_t>(
            side0.status == nav::NavStatus::ok
            || side0.status == nav::NavStatus::clamped);
        door.approach_valid[1] = static_cast<uint8_t>(
            side1.status == nav::NavStatus::ok
            || side1.status == nav::NavStatus::clamped);
        if (door.approach_valid[0]) {
            door.approach_positions[0] = side0.position;
        }
        if (door.approach_valid[1]) {
            door.approach_positions[1] = side1.position;
        }
        if (!door.approach_valid[0] || !door.approach_valid[1]) continue;
        source.door_links.push_back({
            .start = side0.position,
            .end = side1.position,
            .radius = config.cell_size,
            .door_index = door.door_index,
            .active_obstacle_state = door.closed_obstacle_state,
            .side = 0,
        });
        source.door_links.push_back({
            .start = side1.position,
            .end = side0.position,
            .radius = config.cell_size,
            .door_index = door.door_index,
            .active_obstacle_state = door.closed_obstacle_state,
            .side = 1,
        });
    }
    return true;
}

} // namespace

void clear_preview_pointer_action(PreviewInputSample& sample) noexcept
{
    sample.flags &= ~(preview_input_click_target | preview_input_click_door);
}

bool set_preview_click_target(
    PreviewInputSample& sample,
    const glm::vec3& target) noexcept
{
    clear_preview_pointer_action(sample);
    if (!finite(target)) return false;

    sample.click_target = target;
    sample.door_bounds_min = {};
    sample.door_bounds_max = {};
    sample.door_index = UINT32_MAX;
    sample.flags |= preview_input_click_target;
    return true;
}

bool set_preview_click_door(
    PreviewInputSample& sample,
    uint32_t door_index,
    const glm::vec3& bounds_min,
    const glm::vec3& bounds_max) noexcept
{
    clear_preview_pointer_action(sample);
    if (door_index == UINT32_MAX
        || !finite(bounds_min)
        || !finite(bounds_max)
        || glm::any(glm::greaterThan(bounds_min, bounds_max))) {
        return false;
    }

    sample.click_target = {};
    sample.door_bounds_min = bounds_min;
    sample.door_bounds_max = bounds_max;
    sample.door_index = door_index;
    sample.flags |= preview_input_click_door;
    return true;
}

struct ToolsetPreviewSession::Impl {
    // The F9 v1 contract has exactly one locally controlled test actor. Keep
    // that true singleton scalar; navigation and renderer boundaries retain
    // their batch paths for the many-agent systems that actually own arrays.
    nav::NavAreaBuildSource nav_source;
    Vector<uint8_t> nav_obstacle_active;
    Vector<nav::NavDoorObstacleRow> nav_doors;
    Vector<ObjectHandle> door_handles;
    Vector<PreviewDoorVisualState> door_visual_states;
    nav::NavWorldState nav_world;
    nav::NavRouteArena route_arena;
    Vector<uint32_t> rebuilt_tile_keys;
    Vector<glm::vec3> route_corners;
    Vector<nav::NavDebugTriangle> navigation_debug_triangles;
    ObjectHandle area{};
    ObjectHandle actor{};
    uint32_t nav_agent = UINT32_MAX;
    uint32_t route_corner = 0;
    uint32_t route_end = 0;
    uint32_t pending_door = UINT32_MAX;
    uint8_t pending_door_side = 0;
    uint32_t route_action_door = UINT32_MAX;
    uint8_t route_action_side = 0;
    glm::vec3 route_action_bounds_min{0.0f};
    glm::vec3 route_action_bounds_max{0.0f};
    ObjectSpatialState spatial;
    PreviewCameraState camera;
    PreviewActorLocomotion locomotion = PreviewActorLocomotion::idle;
    glm::vec3 debug_requested_target{0.0f};
    glm::vec3 route_destination{0.0f};
    float walk_rate = 0.0f;
    float clearance = 0.0f;
    float radius_class = 0.0f;
    nav::NavStatus debug_path_status = nav::NavStatus::rejected;
    uint64_t debug_revision = 0;
    bool active = false;
    bool navigation_debug_enabled = false;
    bool debug_has_requested_target = false;

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
        navigation_debug_triangles.clear();
        nav_world = nav::NavWorldState{};
        nav_source = nav::NavAreaBuildSource{};
        nav_obstacle_active.clear();
        nav_doors.clear();
        door_handles.clear();
        door_visual_states.clear();
        route_arena.clear();
        rebuilt_tile_keys.clear();
        area = ObjectHandle{};
        actor = ObjectHandle{};
        nav_agent = UINT32_MAX;
        route_corner = 0;
        route_end = 0;
        pending_door = UINT32_MAX;
        pending_door_side = 0;
        route_action_door = UINT32_MAX;
        route_action_side = 0;
        route_action_bounds_min = {};
        route_action_bounds_max = {};
        spatial = ObjectSpatialState{};
        camera = PreviewCameraState{};
        locomotion = PreviewActorLocomotion::idle;
        debug_requested_target = {};
        route_destination = {};
        walk_rate = 0.0f;
        clearance = 0.0f;
        radius_class = 0.0f;
        debug_path_status = nav::NavStatus::rejected;
        debug_revision = 0;
        active = false;
        navigation_debug_enabled = false;
        debug_has_requested_target = false;
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
            && !finite(frame_sample.click_target))
        || ((frame_sample.flags & preview_input_click_door) != 0
            && (!finite(frame_sample.door_bounds_min)
                || !finite(frame_sample.door_bounds_max)
                || glm::any(glm::greaterThan(
                    frame_sample.door_bounds_min,
                    frame_sample.door_bounds_max))))) {
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

    glm::vec3 spawn_position
        = input.spawn_source == PreviewSessionStartInput::SpawnSource::position
        ? input.spawn_position
        : input.spawn_ray.origin;
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
        stop_toolset_preview(session);
        result.status = PreviewStatus::actor_failed;
        result.diagnostic = "Preview actor appearance has no valid PERSPACE clearance";
        return result;
    }
    result.stats.actor_clearance = state.clearance;

    nav::NavGeometry base_geometry;
    const auto tile_stats = nav::build_area_tile_nav_geometry(
        *area, kernel::resman(), base_geometry);
    result.stats.tile_count = tile_stats.tile_count;
    result.stats.authored_surface_triangle_count
        = base_geometry.triangle_count();
    if (!base_geometry.valid() || base_geometry.triangle_count() == 0) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Area has no usable NWN tile walkmesh geometry";
        return result;
    }

    const auto* surfaces = kernel::twodas().get("surfacemat");
    Vector<uint8_t> walkable;
    if (!surfaces
        || nav::build_nav_surface_walkability(*surfaces, walkable)
                .walkable_count
            == 0) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "NWN surface walkability data is unavailable";
        return result;
    }

    nav::NavObjectObstacleSnapshot obstacles;
    nav::build_area_object_nav_obstacles(
        *area, kernel::resman(), obstacles);
    result.stats.obstacle_triangle_count
        = obstacles.geometry.triangle_count();
    if (!obstacles.geometry.valid()) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Area obstacle geometry construction failed";
        return result;
    }

    state.nav_source.surface_vertices = std::move(base_geometry.vertices);
    state.nav_source.surface_indices = std::move(base_geometry.indices);
    state.nav_source.surface_ids = std::move(base_geometry.surface);
    state.nav_source.surface_walkable = std::move(walkable);
    state.nav_source.obstacle_vertices
        = std::move(obstacles.geometry.vertices);
    state.nav_source.obstacle_indices
        = std::move(obstacles.geometry.indices);
    state.nav_source.obstacle_surface_ids
        = std::move(obstacles.geometry.surface);
    state.nav_source.obstacle_owner
        = std::move(obstacles.geometry.owner);
    state.nav_source.width = static_cast<uint32_t>(area->width);
    state.nav_source.height = static_cast<uint32_t>(area->height);
    state.rebuilt_tile_keys.resize(
        static_cast<size_t>(area->width) * static_cast<size_t>(area->height));
    state.nav_obstacle_active = std::move(obstacles.active);
    state.nav_doors = std::move(obstacles.doors);
    state.door_handles.clear();
    state.door_handles.reserve(state.nav_doors.size());
    state.door_visual_states.clear();
    state.door_visual_states.reserve(state.nav_doors.size());
    for (const auto& door : state.nav_doors) {
        state.door_handles.push_back(door.door);
        state.door_visual_states.push_back({
            .door = door.door,
            .state = door.state == nav::NavDoorState::open1
                ? PreviewDoorState::open1
                : door.state == nav::NavDoorState::open2
                ? PreviewDoorState::open2
                : PreviewDoorState::closed,
        });
    }
    if (state.nav_obstacle_active.size() > UINT32_MAX) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Area obstacle state catalog is too large";
        return result;
    }
    state.nav_source.obstacle_state_count
        = static_cast<uint32_t>(state.nav_obstacle_active.size());

    nav::NavTileBuildConfig nav_config;
    const double erosion_cells = std::ceil(
        static_cast<double>(state.clearance)
        / static_cast<double>(nav_config.cell_size));
    if (!std::isfinite(erosion_cells) || erosion_cells < 1.0
        || erosion_cells > std::numeric_limits<uint16_t>::max()) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::actor_failed;
        result.diagnostic
            = "Preview actor clearance is outside the navigation class range";
        return result;
    }
    nav_config.erosion_cells = static_cast<uint16_t>(erosion_cells);
    state.radius_class = static_cast<float>(nav_config.erosion_cells)
        * nav_config.cell_size;
    if (!build_projected_door_links(state.nav_source, state.nav_doors,
            state.nav_obstacle_active, nav_config)) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Door approach projection failed";
        return result;
    }
    nav::NavTiledWorldBuildStats nav_stats;
    if (nav::build_tiled_nav_world(state.nav_source,
            state.nav_obstacle_active, nav_config, state.nav_world, nav_stats)
        != nav::NavStatus::ok) {
        stop_toolset_preview(session);
        result.status = PreviewStatus::navigation_failed;
        result.diagnostic = "Area Recast navigation construction failed";
        return result;
    }
    result.stats.navigation_payload_bytes = nav_stats.payload_bytes;
    result.stats.navigation_polygon_count = nav_stats.polygon_count;
    result.stats.rasterized_obstacle_triangle_count
        = nav_stats.rasterized_obstacle_triangle_count;

    // Path output is retained session storage. Reserve the transform's fixed
    // Detour limits during cold startup so click routes and post-door re-paths
    // do not grow heap-backed arenas on the 60 Hz path.
    state.route_corners.reserve(nav::maximum_nav_path_corners);
    state.route_arena.polygons.reserve(nav::maximum_nav_path_polygons);
    state.route_arena.tile_keys.reserve(nav::maximum_nav_path_polygons);
    state.route_arena.traversals.reserve(nav::maximum_nav_path_corners);

    if (input.spawn_source
        == PreviewSessionStartInput::SpawnSource::navigation_ray) {
        std::array<nav::NavRayProjectionResult, 1> projected{};
        nav::project_nav_rays(state.nav_world,
            std::span<const nav::NavRayProjectionInput>{&input.spawn_ray, 1},
            projected);
        if (projected[0].status != nav::NavStatus::ok) {
            stop_toolset_preview(session);
            result.status = PreviewStatus::navigation_failed;
            result.diagnostic
                = "Preview placement ray did not hit walkable navigation geometry";
            return result;
        }
        spawn_position = projected[0].position;
    }

    const std::array registrations{
        nav::NavAgentRegistrationInput{spawn_position},
    };
    std::array<nav::NavAgentRegistrationResult, 1> registered{};
    nav::register_nav_agents(state.nav_world, registrations, registered);
    if (registered[0].status != nav::NavStatus::ok
        && registered[0].status != nav::NavStatus::clamped) {
        stop_toolset_preview(session);
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

PreviewStatus set_toolset_preview_navigation_debug(
    ToolsetPreviewSession& session, bool enabled)
{
    if (!session.impl_ || !session.impl_->active) {
        return PreviewStatus::idle;
    }

    auto& state = *session.impl_;
    if (state.navigation_debug_enabled == enabled) {
        return PreviewStatus::ok;
    }
    if (!enabled) {
        state.navigation_debug_triangles.clear();
        state.navigation_debug_enabled = false;
        ++state.debug_revision;
        return PreviewStatus::ok;
    }

    const auto stats = nav::collect_nav_debug_triangles(
        state.nav_world, state.navigation_debug_triangles);
    if (stats.output_count == 0) {
        state.navigation_debug_triangles.clear();
        return PreviewStatus::navigation_failed;
    }
    state.navigation_debug_enabled = true;
    ++state.debug_revision;
    return PreviewStatus::ok;
}

PreviewNavigationDebugView toolset_preview_navigation_debug(
    const ToolsetPreviewSession& session) noexcept
{
    if (!session.impl_ || !session.impl_->active
        || !session.impl_->navigation_debug_enabled) {
        return {};
    }

    const auto& state = *session.impl_;
    return {
        .triangles = state.navigation_debug_triangles,
        .route_corners = state.route_corners,
        .requested_target = state.debug_requested_target,
        .active_route_corner = state.route_corner,
        .path_status = state.debug_path_status,
        .revision = state.debug_revision,
        .enabled = true,
        .has_requested_target = state.debug_has_requested_target,
    };
}

std::span<const PreviewDoorVisualState>
toolset_preview_door_visual_states(
    const ToolsetPreviewSession& session) noexcept
{
    if (!session.impl_ || !session.impl_->active) return {};
    return session.impl_->door_visual_states;
}

bool preview_door_requires_interaction(
    std::span<const PreviewDoorVisualState> doors,
    uint32_t door_index) noexcept
{
    return door_index < doors.size()
        && doors[door_index].state == PreviewDoorState::closed;
}

std::span<const ObjectHandle> toolset_preview_door_handles(
    const ToolsetPreviewSession& session) noexcept
{
    if (!session.impl_ || !session.impl_->active) return {};
    return session.impl_->door_handles;
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
    for (auto& door : state.door_visual_states) {
        door.transition = false;
    }
    for (const auto& sample : samples) {
        if (!finite(sample.move_axis) || !finite(sample.look_axis)
            || !std::isfinite(sample.turn_axis)
            || !std::isfinite(sample.zoom_axis)
            || !std::isfinite(sample.zoom_delta)
            || ((sample.flags & preview_input_click_target) != 0
                && !finite(sample.click_target))
            || ((sample.flags & preview_input_click_door) != 0
                && (sample.door_index >= state.nav_doors.size()
                    || !finite(sample.door_bounds_min)
                    || !finite(sample.door_bounds_max)
                    || glm::any(glm::greaterThan(
                        sample.door_bounds_min,
                        sample.door_bounds_max))))
            || ((sample.flags & preview_input_click_target) != 0
                && (sample.flags & preview_input_click_door) != 0)) {
            stats.status = PreviewStatus::invalid_input;
            continue;
        }

        bool debug_route_changed = false;
        const auto clear_route = [&state, &debug_route_changed]() {
            debug_route_changed = debug_route_changed
                || !state.route_corners.empty() || state.route_corner != 0;
            state.route_corners.clear();
            state.route_arena.clear();
            state.route_corner = 0;
            state.route_end = 0;
            state.pending_door = UINT32_MAX;
            state.pending_door_side = 0;
            state.route_action_door = UINT32_MAX;
            state.route_action_side = 0;
            state.route_action_bounds_min = {};
            state.route_action_bounds_max = {};
        };
        const auto assign_route = [&](const glm::vec3& destination) {
            const std::array request{
                nav::NavPathRequest{state.spatial.position, destination},
            };
            std::array<nav::NavPathResult, 1> path_result{};
            nav::find_nav_paths(state.nav_world, request, state.route_corners,
                state.route_arena, path_result);
            state.debug_requested_target = destination;
            state.debug_path_status = path_result[0].status;
            state.debug_has_requested_target = true;
            debug_route_changed = true;
            if (path_result[0].status != nav::NavStatus::ok
                && path_result[0].status != nav::NavStatus::clamped) {
                clear_route();
                return false;
            }

            state.route_destination = destination;
            state.route_corner = path_result[0].corner_offset;
            state.route_end
                = path_result[0].corner_offset + path_result[0].corner_count;
            state.pending_door = UINT32_MAX;
            state.pending_door_side = 0;
            if (path_result[0].traversal_count > 0) {
                const auto& traversal = state.route_arena.traversals[path_result[0].traversal_offset];
                const uint32_t approach = path_result[0].corner_offset
                    + traversal.corner;
                if (approach >= state.route_end) {
                    clear_route();
                    return false;
                }
                state.route_end = approach + 1;
                state.pending_door = traversal.door_index;
                state.pending_door_side = traversal.side;
                state.route_corners.resize(state.route_end);
            }
            while (state.route_corner < state.route_end) {
                if (horizontal_distance_squared(state.spatial.position,
                        state.route_corners[state.route_corner])
                    > route_corner_epsilon * route_corner_epsilon) {
                    break;
                }
                ++state.route_corner;
            }
            return true;
        };

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
            clear_route();
        }
        if ((sample.flags & preview_input_click_target) != 0) {
            clear_route();
            ++stats.path_request_count;
            if (!assign_route(sample.click_target)) {
                ++stats.path_failure_count;
            }
        }
        if ((sample.flags & preview_input_click_door) != 0) {
            clear_route();
            const auto& door = state.nav_doors[sample.door_index];
            std::array<uint8_t, 2> side_order{0, 1};
            if (horizontal_distance_squared(state.spatial.position,
                    door.approach_positions[1])
                < horizontal_distance_squared(state.spatial.position,
                    door.approach_positions[0])) {
                std::swap(side_order[0], side_order[1]);
            }
            bool assigned = false;
            for (const uint8_t side : side_order) {
                if (!door.approach_valid[side]) continue;
                ++stats.path_request_count;
                if (!assign_route(door.approach_positions[side])) continue;
                state.route_action_door = sample.door_index;
                state.route_action_side = side;
                state.route_action_bounds_min = sample.door_bounds_min;
                state.route_action_bounds_max = sample.door_bounds_max;
                assigned = true;
                break;
            }
            if (!assigned) {
                clear_route();
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
        const auto route_displacement = [&state, &debug_route_changed]() {
            while (state.route_corner < state.route_end) {
                if (horizontal_distance_squared(state.spatial.position,
                        state.route_corners[state.route_corner])
                    > route_corner_epsilon * route_corner_epsilon) {
                    break;
                }
                ++state.route_corner;
                debug_route_changed = true;
            }
            if (state.route_corner >= state.route_end) return glm::vec3{};

            glm::vec3 remaining = state.route_corners[state.route_corner] - state.spatial.position;
            remaining.z = 0.0f;
            const float distance = glm::length(glm::vec2{remaining.x, remaining.y});
            if (distance <= route_corner_epsilon) return glm::vec3{};
            return remaining / distance
                * std::min(distance, state.walk_rate * static_cast<float>(fixed_step_seconds));
        };
        if (direct_input) {
            clear_route();
        }
        if (direct_movement) {
            const glm::vec3 forward = horizontal_facing(state.spatial.orientation);
            const glm::vec3 right{forward.y, -forward.x, 0.0f};
            desired = (forward * movement_axis.y + right * movement_axis.x)
                * state.walk_rate * static_cast<float>(fixed_step_seconds);
        } else if (state.route_corner < state.route_end) {
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
                        clear_route();
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
                    && state.route_corner + 1 < state.route_end) {
                    ++state.route_corner;
                    debug_route_changed = true;
                    desired = route_displacement();
                    if (glm::dot(desired, desired) > movement_axis_epsilon) continue;
                }

                ++stats.blocked_count;
                if (!direct_movement) {
                    clear_route();
                    state.locomotion = PreviewActorLocomotion::idle;
                }
                break;
            }
        }

        while (state.route_corner < state.route_end
            && horizontal_distance_squared(state.spatial.position,
                   state.route_corners[state.route_corner])
                <= route_corner_epsilon * route_corner_epsilon) {
            ++state.route_corner;
            debug_route_changed = true;
        }
        const auto transition_door = [&](uint32_t door_index,
                                         nav::NavDoorState target,
                                         uint8_t side) {
            if (door_index >= state.nav_doors.size()
                || door_index >= state.door_visual_states.size()) {
                return false;
            }
            auto& door = state.nav_doors[door_index];
            const std::array changes{
                nav::NavObstacleStateChange{
                    .obstacle_state = door.closed_obstacle_state,
                    .active = static_cast<uint8_t>(
                        target == nav::NavDoorState::closed),
                },
                nav::NavObstacleStateChange{
                    .obstacle_state = door.open1_obstacle_state,
                    .active = static_cast<uint8_t>(
                        target == nav::NavDoorState::open1),
                },
                nav::NavObstacleStateChange{
                    .obstacle_state = door.open2_obstacle_state,
                    .active = static_cast<uint8_t>(
                        target == nav::NavDoorState::open2),
                },
            };
            nav::NavTileRebuildStats rebuild;
            if (nav::rebuild_nav_tiles(state.nav_world, state.nav_source,
                    changes, state.rebuilt_tile_keys, rebuild)
                != nav::NavStatus::ok) {
                return false;
            }
            for (const auto& change : changes) {
                state.nav_obstacle_active[change.obstacle_state]
                    = change.active;
            }

            door.state = target;
            auto& visual = state.door_visual_states[door_index];
            visual.state = target == nav::NavDoorState::open1
                ? PreviewDoorState::open1
                : target == nav::NavDoorState::open2
                ? PreviewDoorState::open2
                : PreviewDoorState::closed;
            visual.side = side;
            visual.transition = true;
            stats.door_open_count += static_cast<size_t>(
                target != nav::NavDoorState::closed);
            stats.door_close_count += static_cast<size_t>(
                target == nav::NavDoorState::closed);
            stats.rebuilt_tile_count += rebuild.rebuilt_tile_count;
            stats.door_rebuild_nanoseconds += rebuild.total_nanoseconds;
            if (state.navigation_debug_enabled) {
                nav::collect_nav_debug_triangles(state.nav_world,
                    state.navigation_debug_triangles);
                ++state.debug_revision;
            }
            return true;
        };

        if (!direct_input && state.route_corner >= state.route_end) {
            if (state.pending_door != UINT32_MAX) {
                const uint32_t door_index = state.pending_door;
                const uint8_t side = state.pending_door_side;
                const auto target = side == 0
                    ? nav::NavDoorState::open1
                    : nav::NavDoorState::open2;
                const glm::vec3 destination = state.route_destination;
                state.pending_door = UINT32_MAX;
                if (!transition_door(door_index, target, side)) {
                    clear_route();
                    ++stats.path_failure_count;
                } else if (!assign_route(destination)) {
                    ++stats.path_failure_count;
                }
            } else if (state.route_action_door != UINT32_MAX) {
                const uint32_t door_index = state.route_action_door;
                if (door_index >= state.nav_doors.size()) {
                    clear_route();
                    ++stats.path_failure_count;
                } else {
                    const auto current = state.nav_doors[door_index].state;
                    if (current != nav::NavDoorState::closed
                        && overlaps_expanded_bounds_xy(
                            state.spatial.position,
                            state.route_action_bounds_min,
                            state.route_action_bounds_max,
                            state.radius_class)) {
                        ++stats.door_close_blocked_count;
                        clear_route();
                    } else {
                        const uint8_t side = current == nav::NavDoorState::open1
                            ? 0
                            : current == nav::NavDoorState::open2
                            ? 1
                            : state.route_action_side;
                        const auto target = current == nav::NavDoorState::closed
                            ? (state.route_action_side == 0
                                      ? nav::NavDoorState::open1
                                      : nav::NavDoorState::open2)
                            : nav::NavDoorState::closed;
                        if (!transition_door(door_index, target, side)) {
                            ++stats.path_failure_count;
                        }
                        clear_route();
                    }
                }
            }
        }

        if (state.navigation_debug_enabled && debug_route_changed) {
            ++state.debug_revision;
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
