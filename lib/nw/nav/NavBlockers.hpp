#pragma once

#include "NavGeometry.hpp"
#include "NavWorld.hpp"

namespace nw::nav {

struct NavBlockerOverlapStats {
    size_t base_triangle_count = 0;
    size_t blocker_triangle_count = 0;
    size_t candidate_pair_count = 0;
    size_t overlap_count = 0;
    size_t duplicate_count = 0;
    size_t rejected_base_triangle_count = 0;
    size_t rejected_blocker_triangle_count = 0;
};

/// Replaces output with sorted unique (blocker owner, base triangle) rows.
/// Base triangles are indexed in a flat 10 m area-cell CSR before blocker
/// testing. Projected vertical blocker triangles remain line segments and can
/// therefore block floor polygons they cross.
NavStatus build_nav_blocker_overlaps(
    const NavGeometry& base,
    std::span<const uint8_t> surface_walkable,
    const NavGeometry& blockers,
    uint32_t area_width,
    uint32_t area_height,
    Vector<NavBlockerOverlapInput>& output,
    NavBlockerOverlapStats& stats);

} // namespace nw::nav
