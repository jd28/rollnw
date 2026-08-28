#pragma once

#include "../config.hpp"
#include "../objects/ObjectHandle.hpp"

#include <glm/mat4x4.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace nw {
struct Area;
struct ResourceManager;
struct StaticTwoDA;

namespace model {
struct Model;
}
}

namespace nw::nav {

enum class NavGeometryKind : uint8_t {
    tile_wok,
    tile_aabb,
    placeable_pwk,
    door_dwk,
};

enum class NavGeometryNodeSelection : uint8_t {
    all,
    door_closed,
    door_open1,
    door_open2,
};

/// Flat navigation geometry. Per-triangle arrays have indices.size() / 3 rows.
/// The owner keeps this storage alive until its navigation revision changes.
struct NavGeometry {
    Vector<glm::vec3> vertices;
    Vector<uint32_t> indices;
    Vector<uint32_t> surface;
    Vector<NavGeometryKind> kind;
    Vector<uint32_t> owner;
    Vector<int32_t> adjacency;

    void clear();
    [[nodiscard]] size_t triangle_count() const noexcept { return indices.size() / 3; }
    /// Validates only the flat-array shape. Element range and finiteness are
    /// validated by the transforms that consume those elements.
    [[nodiscard]] bool valid() const noexcept;
};

struct NavGeometryModelInput {
    const model::Model* model = nullptr;
    glm::mat4 transform{1.0f};
    NavGeometryKind kind = NavGeometryKind::tile_wok;
    NavGeometryNodeSelection node_selection = NavGeometryNodeSelection::all;
    uint32_t owner = 0;
};

struct NavGeometryAppendStats {
    size_t input_count = 0;
    size_t mesh_count = 0;
    size_t triangle_count = 0;
    size_t rejected_input_count = 0;
    size_t rejected_mesh_count = 0;
    size_t dropped_triangle_count = 0;
};

/// Appends triangle/material rows from NWN walkmesh models. Materials are read
/// only from Model::face_material_ranges, so ordinary render meshes are ignored.
NavGeometryAppendStats append_nav_geometry_models(
    std::span<const NavGeometryModelInput> inputs,
    NavGeometry& output);

struct NavAdjacencyStats {
    size_t triangle_count = 0;
    size_t boundary_edge_count = 0;
    size_t linked_edge_count = 0;
    size_t non_manifold_edge_count = 0;
    size_t rejected_edge_count = 0;
};

/// Rebuilds triangle adjacency from world-space edges welded on a global grid.
/// Edges shared by exactly two triangles link; non-manifold runs remain boundaries.
NavAdjacencyStats build_nav_geometry_adjacency(
    NavGeometry& geometry,
    float quantization_step = 0.001f);

struct NavAreaGeometryStats {
    size_t tile_count = 0;
    size_t wok_tile_count = 0;
    size_t empty_wok_tile_count = 0;
    size_t fallback_tile_count = 0;
    size_t missing_tile_count = 0;
    size_t rejected_tile_count = 0;
    size_t unique_resource_count = 0;
    NavGeometryAppendStats append;
    NavAdjacencyStats adjacency;
};

/// Replaces output with the active area's tile navigation geometry. A present,
/// empty WOK is authoritative and contributes no triangles; only an absent WOK
/// falls back to the tile MDL's AABB sidecar.
NavAreaGeometryStats build_area_tile_nav_geometry(
    const Area& area,
    const ResourceManager& resources,
    NavGeometry& output);

struct NavObjectGeometryStats {
    size_t placeable_count = 0;
    size_t door_count = 0;
    size_t missing_visual_count = 0;
    size_t missing_walkmesh_count = 0;
    size_t empty_walkmesh_count = 0;
    size_t rejected_object_count = 0;
    size_t unique_resource_count = 0;
    NavGeometryAppendStats append;
};

enum class NavDoorState : uint8_t {
    closed = 0,
    open1 = 1,
    open2 = 2,
};

/// NWN-adapter row for one live door. Obstacle states are dense rows in the
/// sibling active array. position/normal and closed_half_depth are authored
/// world-space inputs used to derive projected link endpoints per radius class.
struct NavDoorObstacleRow {
    ObjectHandle door{};
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    std::array<glm::vec3, 2> approach_positions{};
    std::array<uint8_t, 2> approach_valid{};
    float closed_half_depth = 0.0f;
    uint32_t door_index = UINT32_MAX;
    uint32_t closed_obstacle_state = UINT32_MAX;
    uint32_t open1_obstacle_state = UINT32_MAX;
    uint32_t open2_obstacle_state = UINT32_MAX;
    NavDoorState state = NavDoorState::closed;
};

/// Cold NWN import snapshot. Geometry includes static PWKs and all three DWK
/// state variants; active has one binary row per dense obstacle state. Shared
/// Recast code consumes only the flattened geometry and active arrays.
struct NavObjectObstacleSnapshot {
    NavGeometry geometry;
    Vector<uint8_t> active;
    Vector<NavDoorObstacleRow> doors;

    void clear();
};

/// Replaces output with placed placeable PWKs and closed-state door DWKs.
/// Triangle owners are dense, snapshot-local obstacle-state rows; they are not
/// object identifiers. Visual model selection comes from ObjectVisualState
/// populated at object instantiation; this transform does not repeat
/// appearance-table policy.
NavObjectGeometryStats build_area_object_nav_geometry(
    const Area& area,
    const ResourceManager& resources,
    NavGeometry& output);

NavObjectGeometryStats build_area_object_nav_obstacles(
    const Area& area,
    const ResourceManager& resources,
    NavObjectObstacleSnapshot& output);

struct NavSurfaceCatalogStats {
    size_t row_count = 0;
    size_t walkable_count = 0;
    size_t invalid_count = 0;
};

/// Builds a dense row-indexed walkability table from surfacemat.2da.
NavSurfaceCatalogStats build_nav_surface_walkability(
    const StaticTwoDA& surfaces,
    Vector<uint8_t>& output);

} // namespace nw::nav
