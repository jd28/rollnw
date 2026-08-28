#pragma once

#include <nw/nav/NavWorld.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/resources/assets.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace nw::toolset {

enum class PreviewStatus : uint8_t {
    ok,
    idle,
    invalid_input,
    navigation_failed,
    actor_failed,
    output_full,
};

enum PreviewInputFlag : uint32_t {
    preview_input_none = 0,
    preview_input_click_target = 1u << 0,
    preview_input_cancel = 1u << 1,
    preview_input_click_door = 1u << 2,
};

struct PreviewInputSample {
    glm::vec2 move_axis{0.0f};
    float turn_axis = 0.0f;
    glm::vec2 look_axis{0.0f};
    float zoom_axis = 0.0f;
    float zoom_delta = 0.0f;
    glm::vec3 click_target{0.0f};
    glm::vec3 door_bounds_min{0.0f};
    glm::vec3 door_bounds_max{0.0f};
    uint32_t door_index = UINT32_MAX;
    uint32_t flags = preview_input_none;
};

/// Pointer actions are singular SDL events. Each setter replaces any pending
/// door/target action so the latest click is the only one observed by the next
/// fixed tick. Invalid data drops the pending pointer action and returns false.
void clear_preview_pointer_action(PreviewInputSample& sample) noexcept;
[[nodiscard]] bool set_preview_click_target(
    PreviewInputSample& sample,
    const glm::vec3& target) noexcept;
[[nodiscard]] bool set_preview_click_door(
    PreviewInputSample& sample,
    uint32_t door_index,
    const glm::vec3& bounds_min,
    const glm::vec3& bounds_max) noexcept;

struct PreviewCameraState {
    glm::vec3 focus{0.0f};
    float yaw = 0.0f;
    float pitch = -0.65f;
    float distance = 8.0f;
};

struct PreviewFixedStepState {
    double accumulator = 0.0;
};

struct PreviewFixedStepStats {
    PreviewStatus status = PreviewStatus::ok;
    size_t tick_count = 0;
    size_t dropped_tick_count = 0;
};

/// Applies a radial deadzone and rescales the remaining unit-circle range.
/// Invalid input or a deadzone outside [0, 1) produces a zero axis.
[[nodiscard]] glm::vec2 preview_radial_deadzone(
    glm::vec2 axis, float deadzone = 0.20f) noexcept;

/// Converts one rendered-frame sample into fixed 60 Hz samples. Edge flags are
/// and zoom delta are present only in the first emitted row; held axes are
/// copied to every row.
PreviewFixedStepStats build_preview_tick_samples(
    PreviewFixedStepState& state,
    double elapsed_seconds,
    const PreviewInputSample& frame_sample,
    std::span<PreviewInputSample> output);

struct PreviewSessionStartInput {
    enum class SpawnSource : uint8_t {
        position,
        navigation_ray,
    };

    ObjectHandle area{};
    Resource actor;
    glm::vec3 spawn_position{0.0f};
    nav::NavRayProjectionInput spawn_ray;
    PreviewCameraState camera;
    SpawnSource spawn_source = SpawnSource::position;
};

struct PreviewSessionStartStats {
    size_t tile_count = 0;
    size_t authored_surface_triangle_count = 0;
    size_t obstacle_triangle_count = 0;
    size_t rasterized_obstacle_triangle_count = 0;
    size_t navigation_polygon_count = 0;
    size_t navigation_payload_bytes = 0;
    float walk_rate = 0.0f;
    float actor_clearance = 0.0f;
    bool walk_rate_fallback = false;
    bool movement_disabled = false;
};

struct PreviewSessionStartResult {
    PreviewStatus status = PreviewStatus::invalid_input;
    PreviewSessionStartStats stats;
    ObjectHandle actor{};
    std::string diagnostic;

    [[nodiscard]] bool ok() const noexcept { return status == PreviewStatus::ok; }
};

struct PreviewTickStats {
    PreviewStatus status = PreviewStatus::idle;
    size_t sample_count = 0;
    size_t movement_count = 0;
    size_t blocked_count = 0;
    size_t path_request_count = 0;
    size_t path_failure_count = 0;
    size_t door_open_count = 0;
    size_t door_close_count = 0;
    size_t door_close_blocked_count = 0;
    size_t rebuilt_tile_count = 0;
    uint64_t door_rebuild_nanoseconds = 0;
    size_t output_count = 0;
};

/// Renderer-facing state for one live area door. Rows are dense by the
/// snapshot-local door index used by navigation and remain valid until the
/// next preview tick or session stop.
enum class PreviewDoorState : uint8_t {
    closed,
    open1,
    open2,
};

struct PreviewDoorVisualState {
    ObjectHandle door{};
    PreviewDoorState state = PreviewDoorState::closed;
    uint8_t side = 0;
    bool transition = false;
};

/// A pointer hit is a single UI event, so this query is intentionally
/// singular. Out-of-range dense indices do not capture the click.
[[nodiscard]] bool preview_door_requires_interaction(
    std::span<const PreviewDoorVisualState> doors,
    uint32_t door_index) noexcept;

/// Borrowed, backend-neutral rows for the active preview's navigation debug
/// overlay. The spans remain valid until the preview session is advanced,
/// stopped, or its debug state changes.
struct PreviewNavigationDebugView {
    std::span<const nav::NavDebugTriangle> triangles;
    std::span<const glm::vec3> route_corners;
    glm::vec3 requested_target{0.0f};
    uint32_t active_route_corner = 0;
    nav::NavStatus path_status = nav::NavStatus::rejected;
    uint64_t revision = 0;
    bool enabled = false;
    bool has_requested_target = false;
};

enum class PreviewActorLocomotion : uint8_t {
    idle,
    walking_forward,
    walking_backward,
    strafing_left,
    strafing_right,
    turning_left,
    turning_right,
};

/// Headless owner of one preview actor and its navigation state. The pimpl is
/// cold lifetime state; fixed ticks operate on contiguous input/output rows.
class ToolsetPreviewSession {
public:
    ToolsetPreviewSession();
    ~ToolsetPreviewSession();
    ToolsetPreviewSession(ToolsetPreviewSession&&) noexcept;
    ToolsetPreviewSession& operator=(ToolsetPreviewSession&&) noexcept;
    ToolsetPreviewSession(const ToolsetPreviewSession&) = delete;
    ToolsetPreviewSession& operator=(const ToolsetPreviewSession&) = delete;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] ObjectHandle actor() const noexcept;
    [[nodiscard]] const PreviewCameraState& camera() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    friend PreviewSessionStartResult start_toolset_preview(
        ToolsetPreviewSession&, const PreviewSessionStartInput&);
    friend void stop_toolset_preview(ToolsetPreviewSession&) noexcept;
    friend PreviewTickStats tick_toolset_preview(
        ToolsetPreviewSession&, std::span<const PreviewInputSample>,
        std::span<ObjectSpatialState>, std::span<PreviewActorLocomotion>);
    friend nav::NavBatchStats project_toolset_preview_rays(
        const ToolsetPreviewSession&,
        std::span<const nav::NavRayProjectionInput>,
        std::span<nav::NavRayProjectionResult>);
    friend PreviewStatus set_toolset_preview_navigation_debug(
        ToolsetPreviewSession&, bool);
    friend PreviewNavigationDebugView toolset_preview_navigation_debug(
        const ToolsetPreviewSession&) noexcept;
    friend std::span<const PreviewDoorVisualState>
    toolset_preview_door_visual_states(
        const ToolsetPreviewSession&) noexcept;
    friend std::span<const ObjectHandle> toolset_preview_door_handles(
        const ToolsetPreviewSession&) noexcept;
};

PreviewSessionStartResult start_toolset_preview(
    ToolsetPreviewSession& session,
    const PreviewSessionStartInput& input);

void stop_toolset_preview(ToolsetPreviewSession& session) noexcept;

/// Projects finite world-space ray segments onto the active preview's
/// currently walkable navigation polygons.
nav::NavBatchStats project_toolset_preview_rays(
    const ToolsetPreviewSession& session,
    std::span<const nav::NavRayProjectionInput> inputs,
    std::span<nav::NavRayProjectionResult> results);

/// Enables or disables the cold navigation debug snapshot. Enabling walks the
/// current navigation mesh once; disabling releases the snapshot immediately.
PreviewStatus set_toolset_preview_navigation_debug(
    ToolsetPreviewSession& session, bool enabled);

[[nodiscard]] PreviewNavigationDebugView toolset_preview_navigation_debug(
    const ToolsetPreviewSession& session) noexcept;

[[nodiscard]] std::span<const PreviewDoorVisualState>
toolset_preview_door_visual_states(
    const ToolsetPreviewSession& session) noexcept;

/// Dense renderer hit-test candidates. The row index is the door index used by
/// PreviewInputSample and navigation; rows remain valid until session stop.
[[nodiscard]] std::span<const ObjectHandle> toolset_preview_door_handles(
    const ToolsetPreviewSession& session) noexcept;

/// Advances every fixed sample in order, then writes the single v1 preview
/// actor's spatial and locomotion rows. The samples form a time batch; the
/// session itself intentionally owns one locally controlled actor. If either
/// output is empty, no sample is advanced and status is output_full.
PreviewTickStats tick_toolset_preview(
    ToolsetPreviewSession& session,
    std::span<const PreviewInputSample> samples,
    std::span<ObjectSpatialState> spatial_output,
    std::span<PreviewActorLocomotion> locomotion_output);

} // namespace nw::toolset
