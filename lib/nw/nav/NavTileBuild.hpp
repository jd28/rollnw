#pragma once

#include "../config.hpp"
#include "NavWorld.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace nw::nav {

inline constexpr float nav_tile_size = 10.0f;
inline constexpr unsigned short nav_walkable_flag = 0x1;
inline constexpr unsigned short nav_door_link_flag = 0x2;

/// Production tiled-Recast configuration. Both cell dimensions must divide a
/// 10 m NWN tile exactly. The cell and detail defaults are the coarsest
/// configuration that passed the recorded topology and height audit; the
/// caller supplies the radius-class erosion count.
struct NavTileBuildConfig {
    float cell_size = 0.125f;
    float cell_height = 0.10f;
    float agent_height = 2.0f;
    float agent_max_climb = 0.5f;
    uint16_t erosion_cells = 0;
    float max_simplification_error = 1.0f;
    float max_edge_length = 0.0f;
    float detail_sample_distance = 3.0f;
    float detail_sample_max_error = 1.0f;
    uint8_t verts_per_polygon = 6;
};

struct NavTileCoord {
    uint32_t x = 0;
    uint32_t y = 0;

    bool operator==(const NavTileCoord&) const = default;
};

/// Tile-major triangle candidates. offsets has width * height + 1 rows and
/// triangles contains source triangle indices. The caller owns the storage for
/// one geometry revision and may reuse it across radius-class tile builds.
struct NavTileTriangleRanges {
    Vector<uint32_t> offsets;
    Vector<uint32_t> triangles;

    void clear();
    [[nodiscard]] std::span<const uint32_t> tile(
        NavTileCoord coordinate, uint32_t width) const noexcept;
};

struct NavTileRangeStats {
    size_t input_triangle_count = 0;
    size_t tile_count = 0;
    size_t overlap_count = 0;
    NavStatus status = NavStatus::rejected;
};

/// Replaces output with a tile CSR for every triangle AABB intersecting each
/// 10 m tile expanded by border. Geometry is finite world-space, Z-up; invalid
/// dimensions, indices, or values reject the complete output.
NavTileRangeStats build_nav_tile_triangle_ranges(
    std::span<const glm::vec3> vertices,
    std::span<const uint32_t> indices,
    uint32_t width,
    uint32_t height,
    float border,
    NavTileTriangleRanges& output);

/// One already-projected traversal connection. Door indices are dense and
/// snapshot-local; side is 0 or 1. active_obstacle_state is the closed door
/// footprint row which enables this link. Disabled links are deliberately
/// omitted from Detour data rather than filtered during each query.
struct NavDoorLink {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    float radius = 0.0f;
    uint32_t door_index = UINT32_MAX;
    uint32_t active_obstacle_state = UINT32_MAX;
    uint8_t side = 0;
};

/// Immutable, area-sized source protocol shared by every radius-class build.
/// All geometry is finite world-space, Z-up. Obstacle owners are dense rows in
/// [0, obstacle_state_count). The caller owns this storage and must not mutate
/// it while any radius-class world built from it is in use.
struct NavAreaBuildSource {
    Vector<glm::vec3> surface_vertices;
    Vector<uint32_t> surface_indices;
    Vector<uint32_t> surface_ids;
    Vector<uint8_t> surface_walkable;

    Vector<glm::vec3> obstacle_vertices;
    Vector<uint32_t> obstacle_indices;
    Vector<uint32_t> obstacle_surface_ids;
    Vector<uint32_t> obstacle_owner;

    Vector<NavDoorLink> door_links;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t obstacle_state_count = 0;
};

/// Flat input protocol for one 10 m tile plus its Recast border. Triangle-index
/// rows select candidates from the corresponding global index arrays. Surface
/// ids index surface_walkable; missing rows are non-walkable. obstacle_owner
/// indexes obstacle_active. All spans are borrowed for this cold call only.
struct NavTileBuildInput {
    std::span<const glm::vec3> surface_vertices;
    std::span<const uint32_t> surface_indices;
    std::span<const uint32_t> surface_ids;
    std::span<const uint32_t> surface_triangles;
    std::span<const uint8_t> surface_walkable;

    std::span<const glm::vec3> obstacle_vertices;
    std::span<const uint32_t> obstacle_indices;
    std::span<const uint32_t> obstacle_surface_ids;
    std::span<const uint32_t> obstacle_owner;
    std::span<const uint32_t> obstacle_triangles;
    std::span<const uint8_t> obstacle_active;

    std::span<const NavDoorLink> door_links;
    std::span<const uint32_t> door_link_indices;
    NavTileCoord tile;
};

struct NavTileBuildStats {
    size_t input_surface_triangle_count = 0;
    size_t rasterized_surface_triangle_count = 0;
    size_t input_obstacle_triangle_count = 0;
    size_t rasterized_obstacle_triangle_count = 0;
    size_t input_door_link_count = 0;
    size_t enabled_door_link_count = 0;
    size_t compact_span_count = 0;
    size_t contour_count = 0;
    size_t polygon_count = 0;
    size_t vertex_count = 0;
    size_t detail_triangle_count = 0;
    size_t normalized_height_vertex_count = 0;
    size_t payload_bytes = 0;
    uint64_t build_nanoseconds = 0;
    NavStatus status = NavStatus::rejected;
};

/// Move-only Detour tile payload. Ownership transfers to dtNavMesh::addTile on
/// success; otherwise this object releases through dtFree.
class NavTileData {
public:
    NavTileData() noexcept = default;
    ~NavTileData();
    NavTileData(NavTileData&& other) noexcept;
    NavTileData& operator=(NavTileData&& other) noexcept;
    NavTileData(const NavTileData&) = delete;
    NavTileData& operator=(const NavTileData&) = delete;

    [[nodiscard]] const unsigned char* data() const noexcept { return data_; }
    [[nodiscard]] unsigned char* data() noexcept { return data_; }
    [[nodiscard]] int size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return data_ == nullptr; }
    [[nodiscard]] unsigned char* release() noexcept;
    void clear() noexcept;

private:
    unsigned char* data_ = nullptr;
    int size_ = 0;

    friend NavTileBuildStats build_nav_tile_data(
        const NavTileBuildInput&, const NavTileBuildConfig&, NavTileData&);
};

/// Rasterizes authored surfaces and active obstacles, erodes once for the
/// world's radius class, partitions with layer regions, builds a six-vertex
/// convex/detail mesh, and creates one standard Detour tile payload. Empty
/// authored output is successful and produces an empty payload.
NavTileBuildStats build_nav_tile_data(
    const NavTileBuildInput& input,
    const NavTileBuildConfig& config,
    NavTileData& output);

struct NavTiledWorldBuildStats {
    size_t declared_tile_count = 0;
    size_t built_tile_count = 0;
    size_t empty_tile_count = 0;
    size_t rejected_tile_count = 0;
    size_t surface_triangle_overlap_count = 0;
    size_t obstacle_triangle_overlap_count = 0;
    size_t rasterized_surface_triangle_count = 0;
    size_t rasterized_obstacle_triangle_count = 0;
    size_t enabled_door_link_count = 0;
    size_t polygon_count = 0;
    size_t polygon_capacity_per_tile = 0;
    size_t vertex_count = 0;
    size_t payload_bytes = 0;
    size_t polygon_graph_bytes = 0;
    uint64_t tile_build_nanoseconds = 0;
    uint64_t polygon_graph_build_nanoseconds = 0;
    uint64_t total_nanoseconds = 0;
    NavStatus status = NavStatus::rejected;
};

/// Builds one immutable Detour snapshot for one already-eroded radius class.
/// obstacle_active has exactly obstacle_state_count rows. Source and active
/// arrays are borrowed for this cold batch; obstacle state is copied, while
/// source must remain immutable and outlive the resulting world because tile
/// rebuilds address its flat arrays. The world owns its Detour payloads. Any
/// invalid tile rejects the complete snapshot.
NavStatus build_tiled_nav_world(
    const NavAreaBuildSource& source,
    std::span<const uint8_t> obstacle_active,
    const NavTileBuildConfig& config,
    NavWorldState& world,
    NavTiledWorldBuildStats& stats);

struct NavObstacleStateChange {
    uint32_t obstacle_state = UINT32_MAX;
    uint8_t active = 0;
};

struct NavTileRebuildStats {
    size_t input_count = 0;
    size_t changed_state_count = 0;
    size_t duplicate_count = 0;
    size_t affected_tile_count = 0;
    size_t rebuilt_tile_count = 0;
    size_t empty_tile_count = 0;
    size_t rejected_count = 0;
    size_t payload_bytes = 0;
    /// Sum of the affected tiles' build durations. Tiles may build in
    /// parallel; total_nanoseconds is the observed batch latency.
    uint64_t tile_build_nanoseconds = 0;
    uint64_t polygon_graph_build_nanoseconds = 0;
    uint64_t total_nanoseconds = 0;
    NavStatus status = NavStatus::rejected;
};

/// Applies one obstacle-state batch and rebuilds each affected 10 m tile once.
/// Source must be the immutable source used to build world. Repeated identical
/// rows collapse; contradictory repeats reject the complete batch. Rebuilt
/// tile keys are sorted, unique y * area_width + x values in caller storage.
/// A failed build leaves the previous obstacle states and tile payloads
/// installed.
NavStatus rebuild_nav_tiles(NavWorldState& world,
    const NavAreaBuildSource& source,
    std::span<const NavObstacleStateChange> changes,
    std::span<uint32_t> rebuilt_tile_keys,
    NavTileRebuildStats& stats);

} // namespace nw::nav
