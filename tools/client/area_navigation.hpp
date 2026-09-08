#pragma once

#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavTileBuild.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>

#include <span>
#include <string>

namespace nw {
struct Area;
}

namespace nw::toolset {

// One assembled area is the singleton source for many navigation queries.
// Source storage must not move/change while a NavWorld borrows its geometry.
struct AreaNavigationSource {
    nav::NavAreaBuildSource geometry;
    Vector<uint8_t> obstacle_active;
    Vector<nav::NavDoorObstacleRow> doors;
    nav::NavAreaGeometryStats tiles;
    nav::NavObjectGeometryStats obstacles;
};

bool build_area_navigation_source(
    const Area& area, AreaNavigationSource& output, std::string& diagnostic);

// Matches play-preview clearance: authored PERSPACE plus 0.1 m. Unknown or
// non-finite radii reject; scale is applied conservatively in the XY plane.
float creature_navigation_clearance(ObjectHandle creature, glm::vec3 scale);

struct AreaPlacementCandidate {
    uint32_t input_index = 0;
    uint16_t erosion_cells = 0;
};

// Gesture-owned cold cache; command admission instead uses fresh stack storage.
// Non-movable because world borrows source.geometry. Non-creature queries do
// not build or allocate navigation. Input handles are non-owning generation
// checks; candidate sorting and all navigation work use contiguous index rows.
struct AreaPlacementNavigation {
    AreaPlacementNavigation() = default;
    AreaPlacementNavigation(const AreaPlacementNavigation&) = delete;
    AreaPlacementNavigation& operator=(const AreaPlacementNavigation&) = delete;

    AreaNavigationSource source;
    nav::NavWorldState world;
    Vector<AreaPlacementCandidate> candidates;
    Vector<nav::NavDebugTriangle> debug_triangles;
    ObjectHandle area{};
    uint64_t epoch = 0;
    uint64_t revision = 0;
    uint16_t erosion_cells = 0;
    bool source_attempted = false;
    bool world_ready = false;
    std::string diagnostic;
};

struct AreaPlacementResult {
    nav::NavStatus status = nav::NavStatus::rejected;
    uint32_t input_index = UINT32_MAX;
    std::string diagnostic;

    [[nodiscard]] bool ok() const noexcept { return status == nav::NavStatus::ok; }
};

// Read-only admission of proposed spatial rows in one live area. Empty batches
// succeed. Rejects invalid/stale/duplicate/wrong-area rows and malformed or
// out-of-bounds transforms before any navigation work. Creature rows must lie
// on their radius-class surface at the requested XYZ; placeables/items retain free Z
// and non-walkable positioning. No objects, undo entries, or files are written.
// The cache is invalidated by area identity or the toolset mutation epoch.
AreaPlacementResult validate_area_placements(
    AreaPlacementNavigation& navigation,
    ObjectHandle area,
    std::span<const ObjectSpatialState> rows);

// Explicit cold overlay collection; not performed by command validation.
bool collect_placement_navigation_debug(AreaPlacementNavigation& navigation);

} // namespace nw::toolset
