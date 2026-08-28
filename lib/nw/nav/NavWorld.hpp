#pragma once

#include "../config.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace nw::nav {

inline constexpr size_t maximum_nav_path_polygons = 2048;
inline constexpr size_t maximum_nav_path_corners = 512;

enum class NavStatus : uint8_t {
    ok,
    clamped,
    blocked,
    off_mesh,
    rejected,
    output_full,
};

/// Detour-backed navigation storage. The implementation pointer is cold
/// ownership state; all per-frame transforms use flat indexed batches below.
struct NavWorldState {
    struct Impl;

    NavWorldState();
    ~NavWorldState();
    NavWorldState(NavWorldState&&) noexcept;
    NavWorldState& operator=(NavWorldState&&) noexcept;
    NavWorldState(const NavWorldState&) = delete;
    NavWorldState& operator=(const NavWorldState&) = delete;

    std::unique_ptr<Impl> impl;
};

struct NavPathRequest {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
};

struct NavPathResult {
    uint32_t corner_offset = 0;
    uint32_t corner_count = 0;
    uint32_t polygon_offset = 0;
    uint32_t polygon_count = 0;
    uint32_t tile_offset = 0;
    uint32_t tile_count = 0;
    uint32_t traversal_offset = 0;
    uint32_t traversal_count = 0;
    NavStatus status = NavStatus::rejected;
};

/// One ordered generated-polygon row in a route corridor. polygon_ref is an
/// opaque snapshot-local value; consumers compare it for equality only. The
/// tile key is y * area_width + x for route invalidation.
struct NavRoutePolygon {
    uint64_t polygon_ref = 0;
    uint32_t tile_key = UINT32_MAX;
};

/// One closed-door off-mesh connection encountered by a route. corner is
/// relative to NavPathResult::corner_offset and identifies the approach point.
struct NavDoorTraversal {
    uint32_t door_index = UINT32_MAX;
    uint32_t corner = UINT32_MAX;
    uint8_t side = 0;
};

/// Caller-owned contiguous route output. Each path result indexes these arrays.
/// tile_keys stores the sorted unique tile set for that route; polygons retain
/// corridor order. The transform clears sizes but retains caller capacity and
/// returns output_full rather than growing these arrays. corner_arena follows
/// the same rule when this route-aware overload is used.
struct NavRouteArena {
    Vector<NavRoutePolygon> polygons;
    Vector<uint32_t> tile_keys;
    Vector<NavDoorTraversal> traversals;

    void clear();
};

/// One active route's untraversed ordered-polygon range. first_remaining_polygon
/// is relative to route.polygon_offset and may equal polygon_count for a
/// completed route.
struct NavRouteInvalidationInput {
    NavPathResult route;
    uint32_t first_remaining_polygon = 0;
};

struct NavRouteInvalidationResult {
    NavStatus status = NavStatus::rejected;
    bool invalidated = false;
};

struct NavBatchStats;

/// Tests a batch of active routes against one sorted unique rebuilt tile-key
/// set. The full-route sidecar rejects the common no-overlap case; an overlap
/// is invalidating only when it occurs in the remaining ordered corridor.
/// This transform performs no allocation.
NavBatchStats invalidate_nav_routes(
    const NavRouteArena& arena,
    std::span<const NavRouteInvalidationInput> inputs,
    std::span<const uint32_t> rebuilt_tile_keys,
    std::span<NavRouteInvalidationResult> results);

struct NavBatchStats {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t rejected_count = 0;
    size_t clamped_count = 0;
    size_t blocked_count = 0;
};

enum class NavDebugPolygonState : uint8_t {
    walkable,
    blocked,
};

/// Backend-neutral debug row for one triangulated navigation polygon. Output
/// is world-space, Z-up, and valid until the caller replaces its own batch.
struct NavDebugTriangle {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    glm::vec3 c{0.0f};
    NavDebugPolygonState state = NavDebugPolygonState::blocked;
};

/// Replaces triangles with the current navigation polygon batch. This cold
/// debug transform walks Detour storage only when explicitly requested; no
/// Detour type crosses the boundary. Invalid polygons are dropped and counted.
NavBatchStats collect_nav_debug_triangles(
    const NavWorldState& world,
    Vector<NavDebugTriangle>& triangles);

/// Finite world-space ray segment. A point on the segment is
/// origin + displacement * fraction, where fraction is in [0, 1].
struct NavRayProjectionInput {
    glm::vec3 origin{0.0f};
    glm::vec3 displacement{0.0f};
};

struct NavRayProjectionResult {
    glm::vec3 position{0.0f};
    float fraction = 0.0f;
    NavStatus status = NavStatus::rejected;
};

/// Projects finite ray segments onto currently walkable navigation polygons.
/// Results are parallel to inputs; the nearest hit along each segment wins.
NavBatchStats project_nav_rays(
    const NavWorldState& world,
    std::span<const NavRayProjectionInput> inputs,
    std::span<NavRayProjectionResult> results);

/// Clears corner_arena on entry. Results index the resulting shared arena;
/// callers intentionally cannot accumulate path batches across calls.
NavBatchStats find_nav_paths(
    const NavWorldState& world,
    std::span<const NavPathRequest> requests,
    Vector<glm::vec3>& corner_arena,
    std::span<NavPathResult> results);

NavBatchStats find_nav_paths(
    const NavWorldState& world,
    std::span<const NavPathRequest> requests,
    Vector<glm::vec3>& corner_arena,
    NavRouteArena& route_arena,
    std::span<NavPathResult> results);

struct NavAgentRegistrationInput {
    glm::vec3 position{0.0f};
};

struct NavAgentRegistrationResult {
    uint32_t agent = UINT32_MAX;
    glm::vec3 position{0.0f};
    NavStatus status = NavStatus::rejected;
};

NavBatchStats register_nav_agents(
    NavWorldState& world,
    std::span<const NavAgentRegistrationInput> inputs,
    std::span<NavAgentRegistrationResult> results);

NavBatchStats release_nav_agents(
    NavWorldState& world,
    std::span<const uint32_t> agents);

struct NavAgentMotionInput {
    uint32_t agent = UINT32_MAX;
    glm::vec3 position{0.0f};
    glm::vec3 desired_displacement{0.0f};
};

struct NavAgentMotionResult {
    glm::vec3 position{0.0f};
    glm::vec3 applied_displacement{0.0f};
    NavStatus status = NavStatus::rejected;
};

NavBatchStats move_nav_agents(
    NavWorldState& world,
    std::span<const NavAgentMotionInput> inputs,
    std::span<NavAgentMotionResult> results);

} // namespace nw::nav
