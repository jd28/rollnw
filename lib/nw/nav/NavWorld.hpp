#pragma once

#include "../config.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace nw::nav {

struct NavGeometry;

enum class NavStatus : uint8_t {
    ok,
    clamped,
    blocked,
    off_mesh,
    rejected,
    output_full,
};

struct NavBuildStats {
    size_t input_triangle_count = 0;
    size_t walkable_triangle_count = 0;
    size_t tile_count = 0;
    size_t vertex_count = 0;
    size_t polygon_count = 0;
    size_t rejected_triangle_count = 0;
    size_t rejected_tile_count = 0;
    size_t portal_edge_count = 0;
    size_t payload_bytes = 0;
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

/// Replaces world with one Detour tile per NWN area tile. surface_walkable is
/// indexed by NavGeometry::surface; missing rows are non-walkable. Rejects the
/// complete build if dimensions or Detour reference limits cannot represent it.
NavStatus build_nav_world(
    const NavGeometry& geometry,
    std::span<const uint8_t> surface_walkable,
    uint32_t area_width,
    uint32_t area_height,
    NavWorldState& world,
    NavBuildStats& stats);

struct NavPathRequest {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    /// Minimum horizontal distance of returned corners from navigation walls,
    /// in metres. Zero preserves point-path behavior.
    float clearance = 0.0f;
};

struct NavPathResult {
    uint32_t corner_offset = 0;
    uint32_t corner_count = 0;
    NavStatus status = NavStatus::rejected;
};

struct NavBatchStats {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t rejected_count = 0;
    size_t clamped_count = 0;
    size_t blocked_count = 0;
};

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

struct NavBlockerOverlapInput {
    uint32_t owner = 0;
    uint32_t triangle = UINT32_MAX;
};

struct NavBlockerBuildStats {
    size_t input_count = 0;
    size_t owner_count = 0;
    size_t overlap_count = 0;
    size_t duplicate_count = 0;
    size_t rejected_count = 0;
};

/// Replaces the blocker table with sorted owner/triangle overlaps. Every
/// configured owner begins closed; repeated pairs are collapsed. Triangle
/// indices address the NavGeometry passed to build_nav_world.
NavBlockerBuildStats configure_nav_blockers(
    NavWorldState& world,
    std::span<const NavBlockerOverlapInput> overlaps);

struct NavBlockerStateInput {
    uint32_t owner = 0;
    bool closed = true;
};

/// Applies owner state changes over the configured CSR ranges. Overlapping
/// owners compose through per-polygon reference counts.
NavBatchStats set_nav_blockers_closed(
    NavWorldState& world,
    std::span<const NavBlockerStateInput> inputs);

/// Clears corner_arena on entry. Results index the resulting shared arena;
/// callers intentionally cannot accumulate path batches across calls.
NavBatchStats find_nav_paths(
    const NavWorldState& world,
    std::span<const NavPathRequest> requests,
    Vector<glm::vec3>& corner_arena,
    std::span<NavPathResult> results);

struct NavAgentRegistrationInput {
    glm::vec3 position{0.0f};
    /// Minimum horizontal distance from navigation walls, in metres.
    /// Zero preserves point-agent behavior.
    float clearance = 0.0f;
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
