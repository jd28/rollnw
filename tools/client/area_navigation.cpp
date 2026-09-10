#include "area_navigation.hpp"

#include "object_edits.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nw::toolset {
namespace {

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool authored_area_object(ObjectType type)
{
    switch (type) {
    case ObjectType::creature:
    case ObjectType::door:
    case ObjectType::encounter:
    case ObjectType::item:
    case ObjectType::placeable:
    case ObjectType::sound:
    case ObjectType::store:
    case ObjectType::trigger:
    case ObjectType::waypoint:
        return true;
    default:
        return false;
    }
}

bool complete_geometry(const AreaNavigationSource& source)
{
    const auto& tiles = source.tiles;
    const auto& objects = source.obstacles;
    // Missing PWK/DWK is the existing NWN adapter's no-authored-obstacle case,
    // not permission to ignore a present but malformed walkmesh.
    return tiles.missing_tile_count == 0 && tiles.rejected_tile_count == 0
        && tiles.append.rejected_input_count == 0
        && tiles.append.rejected_mesh_count == 0
        && tiles.append.dropped_triangle_count == 0
        && objects.missing_visual_count == 0 && objects.rejected_object_count == 0
        && objects.append.rejected_input_count == 0
        && objects.append.rejected_mesh_count == 0
        && objects.append.dropped_triangle_count == 0;
}

} // namespace

bool build_area_navigation_source(
    const Area& area, AreaNavigationSource& output, std::string& diagnostic)
{
    output = {};
    diagnostic.clear();
    nav::NavGeometry ground;
    output.tiles = nav::build_area_tile_nav_geometry(area, kernel::resman(), ground);
    if (!ground.valid() || ground.triangle_count() == 0) {
        diagnostic = "Area has no usable NWN tile walkmesh geometry";
        return false;
    }
    const auto* surfaces = kernel::twodas().get("surfacemat");
    auto& source = output.geometry;
    if (!surfaces || nav::build_nav_surface_walkability(*surfaces, source.surface_walkable).walkable_count == 0) {
        diagnostic = "NWN surface walkability data is unavailable";
        return false;
    }
    nav::NavObjectObstacleSnapshot obstacles;
    output.obstacles = nav::build_area_object_nav_obstacles(
        area, kernel::resman(), obstacles);
    if (!obstacles.geometry.valid() || obstacles.active.size() > UINT32_MAX) {
        diagnostic = "Area obstacle geometry construction failed";
        return false;
    }
    source.surface_vertices = std::move(ground.vertices);
    source.surface_indices = std::move(ground.indices);
    source.surface_ids = std::move(ground.surface);
    source.obstacle_vertices = std::move(obstacles.geometry.vertices);
    source.obstacle_indices = std::move(obstacles.geometry.indices);
    source.obstacle_surface_ids = std::move(obstacles.geometry.surface);
    source.obstacle_owner = std::move(obstacles.geometry.owner);
    source.width = static_cast<uint32_t>(area.width);
    source.height = static_cast<uint32_t>(area.height);
    source.obstacle_state_count = static_cast<uint32_t>(obstacles.active.size());
    output.obstacle_active = std::move(obstacles.active);
    output.doors = std::move(obstacles.doors);
    return true;
}

float creature_navigation_clearance(ObjectHandle creature, glm::vec3 scale)
{
    const auto* visual = kernel::objects().components().find_visual(creature);
    if (creature.type != ObjectType::creature || !kernel::objects().valid(creature)
        || !visual || !finite(scale) || scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
        return -1.0f;
    }
    const auto* appearance = kernel::rules().appearances.get(Appearance::make(visual->appearance));
    if (!appearance || !std::isfinite(appearance->personal_space)
        || appearance->personal_space < 0.0f) {
        return -1.0f;
    }
    const float clearance = appearance->personal_space * std::max(scale.x, scale.y) + 0.1f;
    return std::isfinite(clearance) ? clearance : -1.0f;
}

AreaPlacementResult validate_area_placements(
    AreaPlacementNavigation& navigation,
    ObjectHandle area,
    std::span<const ObjectSpatialState> rows)
{
    const auto* live_area = area.type == ObjectType::area ? kernel::objects().get<Area>(area) : nullptr;
    if (!live_area || live_area->width <= 0 || live_area->height <= 0 || rows.size() > UINT32_MAX) {
        return {nav::NavStatus::rejected, UINT32_MAX, "Placement area or batch is invalid or stale"};
    }
    const float max_x = static_cast<float>(live_area->width) * nav::nav_tile_size;
    const float max_y = static_cast<float>(live_area->height) * nav::nav_tile_size;
    const auto epoch = object_mutation_state().epoch;
    if (navigation.area != area || navigation.epoch != epoch) {
        navigation.world = {};
        navigation.source = {};
        navigation.debug_triangles.clear();
        navigation.area = area;
        navigation.epoch = epoch;
        navigation.source_attempted = false;
        navigation.erosion_cells = 0;
        navigation.world_ready = false;
        navigation.diagnostic.clear();
        ++navigation.revision;
    }
    navigation.candidates.clear();
    nav::NavTileBuildConfig config;
    try {
        for (size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            const auto* live = kernel::objects().components().find_spatial(row.owner);
            if (!authored_area_object(row.owner.type)
                || !kernel::objects().valid(row.owner) || !live
                || row.area != area.id || live->area != area.id
                || !finite(row.position) || !finite(row.orientation) || !finite(row.scale)
                || row.scale.x <= 0.0f || row.scale.y <= 0.0f || row.scale.z <= 0.0f
                || row.position.x < 0.0f || row.position.x > max_x
                || row.position.y < 0.0f || row.position.y > max_y) {
                return {nav::NavStatus::rejected, static_cast<uint32_t>(index),
                    "Placement transform or target is invalid, stale, or out of bounds"};
            }
            for (size_t prior = 0; prior < index; ++prior) {
                if (rows[prior].owner == row.owner) {
                    return {nav::NavStatus::rejected, static_cast<uint32_t>(index),
                        "Placement batch contains duplicate objects"};
                }
            }
            if (row.owner.type != ObjectType::creature) continue;
            const float clearance = creature_navigation_clearance(row.owner, row.scale);
            const double cells = std::ceil(static_cast<double>(clearance) / static_cast<double>(config.cell_size));
            if (!std::isfinite(cells) || cells < 1.0 || cells > UINT16_MAX) {
                return {nav::NavStatus::rejected, static_cast<uint32_t>(index),
                    "Creature has no valid navigation clearance (PERSPACE and scale)"};
            }
            navigation.candidates.push_back({static_cast<uint32_t>(index), static_cast<uint16_t>(cells)});
        }
        if (navigation.candidates.empty()) return {nav::NavStatus::ok, UINT32_MAX, {}};

        if (!navigation.source_attempted) {
            navigation.source_attempted = true;
            if (build_area_navigation_source(*live_area, navigation.source, navigation.diagnostic)
                && !complete_geometry(navigation.source)) {
                navigation.diagnostic = "Area navigation geometry is incomplete; creature placement cannot be verified";
            }
        }
        if (!navigation.diagnostic.empty()) {
            return {nav::NavStatus::rejected, navigation.candidates.front().input_index, navigation.diagnostic};
        }
        std::sort(navigation.candidates.begin(), navigation.candidates.end(), [](const auto& left, const auto& right) {
            return left.erosion_cells < right.erosion_cells;
        });
        for (const auto candidate : navigation.candidates) {
            if (navigation.erosion_cells != candidate.erosion_cells) {
                navigation.world = {};
                navigation.debug_triangles.clear();
                navigation.erosion_cells = candidate.erosion_cells;
                config.erosion_cells = candidate.erosion_cells;
                nav::NavTiledWorldBuildStats stats;
                navigation.world_ready = nav::build_tiled_nav_world(navigation.source.geometry,
                                             navigation.source.obstacle_active, config, navigation.world, stats)
                    == nav::NavStatus::ok;
                ++navigation.revision;
            }
            if (!navigation.world_ready) {
                return {nav::NavStatus::rejected, candidate.input_index, "Creature navigation build failed"};
            }
            const std::array positions{rows[candidate.input_index].position};
            std::array<nav::NavStatus, 1> result{};
            nav::validate_nav_positions(navigation.world, positions, result);
            if (result[0] != nav::NavStatus::ok) {
                return {result[0], candidate.input_index,
                    "Creature must be on walkable ground with sufficient navigation clearance"};
            }
        }
    } catch (const std::bad_alloc&) {
        navigation.world_ready = false;
        navigation.diagnostic = "Creature placement navigation allocation failed";
        return {nav::NavStatus::rejected, UINT32_MAX, navigation.diagnostic};
    } catch (const std::length_error&) {
        navigation.world_ready = false;
        navigation.diagnostic = "Creature placement navigation exceeds container capacity";
        return {nav::NavStatus::rejected, UINT32_MAX, navigation.diagnostic};
    }
    return {nav::NavStatus::ok, UINT32_MAX, {}};
}

bool collect_placement_navigation_debug(AreaPlacementNavigation& navigation)
{
    if (!navigation.world_ready) return false;
    if (!navigation.debug_triangles.empty()) return true;
    try {
        const auto stats = nav::collect_nav_debug_triangles(navigation.world, navigation.debug_triangles);
        if (stats.rejected_count == 0) return true;
    } catch (const std::bad_alloc&) {
    } catch (const std::length_error&) {
    }
    navigation.debug_triangles.clear();
    return false;
}

} // namespace nw::toolset
