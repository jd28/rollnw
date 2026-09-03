#if defined(ROLLNW_ALL_WALKMESH_RENDERER)
#include <nw/formats/StaticTwoDA.hpp>
#endif
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/nav/NavGeometry.hpp>
#include <nw/nav/NavTileBuild.hpp>
#if defined(ROLLNW_ALL_WALKMESH_RENDERER)
#include <nw/render/model_instance_animation.hpp>
#include <nw/render/nwn/model_loader.hpp>
#endif
#include <nw/resources/ResourceManager.hpp>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>

#include "../../tests/test_nwn_root.hpp"

namespace {

struct WalkmeshStats {
    uint64_t resources = 0;
    uint64_t binary_resources = 0;
    uint64_t bytes = 0;
    uint64_t nodes = 0;
    uint64_t vertices = 0;
    uint64_t triangles = 0;
    uint64_t invalid = 0;
    uint64_t empty = 0;
    uint64_t maximum_vertices = 0;
    uint64_t maximum_triangles = 0;
    float maximum_horizontal_span = 0.0f;
    nw::String maximum_vertices_resource;
    nw::String maximum_triangles_resource;
    nw::String maximum_horizontal_span_resource;
    uint64_t horizontal_triangles = 0;
    uint64_t vertical_triangles = 0;
    uint64_t angled_triangles = 0;
    glm::vec3 position_min{std::numeric_limits<float>::max()};
    glm::vec3 position_max{std::numeric_limits<float>::lowest()};
};

struct DoorWalkmeshStateStats {
    uint64_t closed_nodes = 0;
    uint64_t open1_nodes = 0;
    uint64_t open2_nodes = 0;
    uint64_t other_open_nodes = 0;
    uint64_t unclassified_nodes = 0;
    nw::Vector<nw::String> unclassified_examples;
};

struct RecastMergeStats {
    uint64_t resources = 0;
    uint64_t built = 0;
    uint64_t rejected = 0;
    uint64_t empty = 0;
    uint64_t no_walkable_input = 0;
    uint64_t lost_walkable_input = 0;
    uint64_t input_triangles = 0;
    uint64_t merged_polygons = 0;
    uint64_t merged_vertices = 0;
    uint64_t detail_triangles = 0;
    uint64_t payload_bytes = 0;
    uint64_t height_samples = 0;
    uint64_t height_projection_failures = 0;
    uint64_t height_error_violations = 0;
    uint64_t authored_components = 0;
    uint64_t recast_components = 0;
    uint64_t component_split_resources = 0;
    uint64_t component_join_resources = 0;
    float maximum_height_error = 0.0f;
    uint64_t worst_resource_triangles = 0;
    uint64_t worst_resource_polygons = 0;
    nw::String worst_resource;
    nw::String maximum_height_error_sample;
    nw::Vector<nw::String> lost_walkable_examples;
    nw::Vector<nw::String> component_mismatch_examples;
    int64_t elapsed_ns = 0;
};

#if defined(ROLLNW_ALL_WALKMESH_RENDERER)
struct DoorModelAnimationStats {
    uint64_t model_rows = 0;
    uint64_t unique_models = 0;
    uint64_t missing_models = 0;
    uint64_t missing_dwks = 0;
    uint64_t unavailable_model_payloads = 0;
    uint64_t invalid_models = 0;
    uint64_t declared_family_clips = 0;
    uint64_t backend_sampleable_family_clips = 0;
    uint64_t zero_duration_family_clips = 0;
    uint64_t declared_hold_clips = 0;
    uint64_t zero_duration_hold_clips = 0;
    float maximum_horizontal_extent = 0.0f;
    float maximum_vertical_extent = 0.0f;
};

bool door_animation_family(std::string_view name) noexcept
{
    constexpr std::array names{
        std::string_view{"opening1"},
        std::string_view{"opened1"},
        std::string_view{"closing1"},
        std::string_view{"opening2"},
        std::string_view{"opened2"},
        std::string_view{"closing2"},
        std::string_view{"closed"},
    };
    return std::ranges::find(names, name) != names.end();
}

bool door_hold_animation(std::string_view name) noexcept
{
    return name == "opened1" || name == "opened2" || name == "closed";
}

void append_door_models(
    const nw::StaticTwoDA* table,
    std::string_view column,
    nw::Vector<nw::Resref>& models,
    DoorModelAnimationStats& stats)
{
    if (!table) return;
    for (size_t row = 0; row < table->rows(); ++row) {
        nw::StringView model;
        if (!table->get_to(row, column, model, false)
            || model.empty() || model == "****") {
            continue;
        }
        ++stats.model_rows;
        models.emplace_back(model);
    }
}
#endif

size_t walkmesh_kind(nw::ResourceType::type type)
{
    switch (type) {
    case nw::ResourceType::wok:
        return 0;
    case nw::ResourceType::pwk:
        return 1;
    case nw::ResourceType::dwk:
        return 2;
    default:
        return 3;
    }
}

float percentile(nw::Vector<float> values, float fraction)
{
    if (values.empty()) return 0.0f;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::floor(
        fraction * static_cast<float>(values.size() - 1)));
    return values[index];
}

uint32_t count_authored_components(nw::nav::NavGeometry& geometry,
    std::span<const uint8_t> surface_walkable)
{
    nw::nav::build_nav_geometry_adjacency(geometry);
    nw::Vector<uint8_t> visited(geometry.triangle_count(), 0u);
    nw::Vector<uint32_t> stack;
    const auto traversable = [&geometry, surface_walkable](uint32_t triangle) {
        if (triangle >= geometry.triangle_count()
            || geometry.surface[triangle] >= surface_walkable.size()
            || surface_walkable[geometry.surface[triangle]] == 0) {
            return false;
        }
        const size_t offset = static_cast<size_t>(triangle) * 3;
        const auto& a = geometry.vertices[geometry.indices[offset]];
        const auto& b = geometry.vertices[geometry.indices[offset + 1]];
        const auto& c = geometry.vertices[geometry.indices[offset + 2]];
        return std::abs(glm::cross(b - a, c - a).z) > 1.0e-8f;
    };
    uint32_t components = 0;
    for (uint32_t triangle = 0; triangle < geometry.triangle_count(); ++triangle) {
        if (visited[triangle] || !traversable(triangle)) continue;
        ++components;
        stack.clear();
        stack.push_back(triangle);
        visited[triangle] = 1u;
        while (!stack.empty()) {
            const uint32_t current = stack.back();
            stack.pop_back();
            for (size_t edge = 0; edge < 3; ++edge) {
                const int32_t neighbor = geometry.adjacency[current * 3 + edge];
                if (neighbor < 0
                    || visited[static_cast<size_t>(neighbor)]
                    || !traversable(static_cast<uint32_t>(neighbor))) {
                    continue;
                }
                visited[static_cast<size_t>(neighbor)] = 1u;
                stack.push_back(static_cast<uint32_t>(neighbor));
            }
        }
    }
    return components;
}

bool authored_height_at_point(const nw::nav::NavGeometry& geometry,
    std::span<const uint8_t> surface_walkable, float x, float y,
    float reference_height, float& authored_height)
{
    constexpr double epsilon = 1.0e-5;
    bool found = false;
    float best_error = std::numeric_limits<float>::max();
    for (size_t triangle = 0; triangle < geometry.triangle_count(); ++triangle) {
        const uint32_t surface = geometry.surface[triangle];
        if (surface >= surface_walkable.size()
            || surface_walkable[surface] == 0) {
            continue;
        }
        const size_t offset = triangle * 3;
        const auto& a = geometry.vertices[geometry.indices[offset]];
        const auto& b = geometry.vertices[geometry.indices[offset + 1]];
        const auto& c = geometry.vertices[geometry.indices[offset + 2]];
        const double ax = static_cast<double>(a.x);
        const double ay = static_cast<double>(a.y);
        const double az = static_cast<double>(a.z);
        const double bx = static_cast<double>(b.x);
        const double by = static_cast<double>(b.y);
        const double bz = static_cast<double>(b.z);
        const double cx = static_cast<double>(c.x);
        const double cy = static_cast<double>(c.y);
        const double cz = static_cast<double>(c.z);
        const double px = static_cast<double>(x);
        const double py = static_cast<double>(y);
        const double denominator
            = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(denominator) <= epsilon) continue;
        const double wa = ((by - cy) * (px - cx)
                              + (cx - bx) * (py - cy))
            / denominator;
        const double wb = ((cy - ay) * (px - cx)
                              + (ax - cx) * (py - cy))
            / denominator;
        const double wc = 1.0 - wa - wb;
        if (wa < -epsilon || wb < -epsilon || wc < -epsilon) continue;
        const float height
            = static_cast<float>(wa * az + wb * bz + wc * cz);
        const float error = std::abs(height - reference_height);
        if (!found || error < best_error) {
            found = true;
            best_error = error;
            authored_height = height;
        }
    }
    return found;
}

std::array<float, 3> detail_vertex(const dtMeshTile& tile,
    const dtPoly& polygon, const dtPolyDetail& detail, uint8_t vertex)
{
    const float* source = nullptr;
    if (vertex < polygon.vertCount) {
        source = &tile.verts[polygon.verts[vertex] * 3];
    } else {
        const uint32_t detail_vertex_index = detail.vertBase
            + static_cast<uint32_t>(vertex - polygon.vertCount);
        source = &tile.detailVerts[detail_vertex_index * 3];
    }
    return {source[0], source[1], source[2]};
}

void measure_surface_error(const nw::nav::NavGeometry& geometry,
    std::span<const uint8_t> surface_walkable,
    const nw::nav::NavTileBuildConfig& config,
    const nw::nav::NavTileData& tile_data, uint32_t authored_components,
    const nw::String& resource_name, RecastMergeStats& stats)
{
    dtNavMesh* mesh = dtAllocNavMesh();
    if (!mesh
        || dtStatusFailed(mesh->init(
            const_cast<unsigned char*>(tile_data.data()),
            tile_data.size(), 0))) {
        dtFreeNavMesh(mesh);
        ++stats.height_projection_failures;
        return;
    }
    const dtMeshTile* tile
        = static_cast<const dtNavMesh&>(*mesh).getTile(0);
    uint32_t recast_components = 0;
    if (tile && tile->header) {
        nw::Vector<uint8_t> visited(
            static_cast<size_t>(tile->header->polyCount), 0u);
        nw::Vector<uint32_t> stack;
        for (uint32_t polygon = 0;
            polygon < static_cast<uint32_t>(tile->header->polyCount); ++polygon) {
            if (visited[polygon]
                || tile->polys[polygon].getType()
                    == DT_POLYTYPE_OFFMESH_CONNECTION) {
                continue;
            }
            ++recast_components;
            stack.clear();
            stack.push_back(polygon);
            visited[polygon] = 1u;
            while (!stack.empty()) {
                const uint32_t current = stack.back();
                stack.pop_back();
                const auto& poly = tile->polys[current];
                for (unsigned edge = 0; edge < poly.vertCount; ++edge) {
                    const unsigned short neighbor = poly.neis[edge];
                    if (neighbor == 0 || (neighbor & DT_EXT_LINK) != 0) continue;
                    const uint32_t next = static_cast<uint32_t>(neighbor - 1);
                    if (next >= visited.size() || visited[next]) continue;
                    visited[next] = 1u;
                    stack.push_back(next);
                }
            }
        }
    }
    stats.authored_components += authored_components;
    stats.recast_components += recast_components;
    stats.component_split_resources += recast_components > authored_components;
    stats.component_join_resources += recast_components < authored_components;
    if (recast_components != authored_components
        && stats.component_mismatch_examples.size() < 16) {
        stats.component_mismatch_examples.push_back(fmt::format(
            "{} authored={} recast={}", resource_name,
            authored_components, recast_components));
    }
    if (tile && tile->header) {
        for (int polygon_index = 0;
            polygon_index < tile->header->offMeshBase; ++polygon_index) {
            const auto& polygon = tile->polys[polygon_index];
            const auto& detail = tile->detailMeshes[polygon_index];
            for (uint32_t triangle = 0; triangle < detail.triCount; ++triangle) {
                const auto* indices
                    = &tile->detailTris[(detail.triBase + triangle) * 4];
                const auto a = detail_vertex(
                    *tile, polygon, detail, indices[0]);
                const auto b = detail_vertex(
                    *tile, polygon, detail, indices[1]);
                const auto c = detail_vertex(
                    *tile, polygon, detail, indices[2]);
                const std::array<float, 3> recast{
                    (a[0] + b[0] + c[0]) / 3.0f,
                    (a[1] + b[1] + c[1]) / 3.0f,
                    (a[2] + b[2] + c[2]) / 3.0f,
                };
                float authored_height = 0.0f;
                if (!authored_height_at_point(geometry, surface_walkable,
                        recast[0], recast[2], recast[1], authored_height)) {
                    ++stats.height_projection_failures;
                    continue;
                }
                const float error = std::abs(recast[1] - authored_height);
                ++stats.height_samples;
                if (error > stats.maximum_height_error) {
                    stats.maximum_height_error = error;
                    stats.maximum_height_error_sample = fmt::format(
                        "{} recast=({}, {}, {}) authored_height={}",
                        resource_name, recast[0], recast[2], recast[1],
                        authored_height);
                }
                stats.height_error_violations
                    += error > config.cell_height + 1.0e-4f;
            }
        }
    }
    dtFreeNavMesh(mesh);
}

} // namespace

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    if (!nw::test::configure_dedicated_server("test_data/user/")) { return 1; }
    nw::ConfigOptions config_options;
    config_options.include_user = false;
    config_options.profile = "nwn1";
    nw::kernel::config().initialize(std::move(config_options));
    nw::test::register_source_nwn1_packages();
    nw::kernel::services().start();

    struct TilePathNodeStats {
        uint64_t tilesets = 0;
        uint64_t tiles = 0;
        uint64_t missing = 0;
        uint64_t malformed = 0;
        uint64_t unknown_value = 0;
        uint64_t non_quarter_turn_orientation = 0;
        std::array<uint64_t, 256> value_counts{};
        std::map<int32_t, uint64_t> orientation_counts;
        nw::String malformed_example;
    } path_nodes;
    nw::kernel::resman().visit([&](const nw::Resource& resource) {
        if (resource.type != nw::ResourceType::set) return;
        const auto* tileset
            = nw::kernel::tilesets().get(resource.resref.view());
        if (!tileset) return;
        ++path_nodes.tilesets;
        path_nodes.tiles += tileset->tiles.size();
        for (size_t tile_index = 0; tile_index < tileset->tiles.size();
            ++tile_index) {
            const auto& tile = tileset->tiles[tile_index];
            ++path_nodes.orientation_counts[tile.path_node_orientation];
            path_nodes.non_quarter_turn_orientation
                += tile.path_node_orientation % 90 != 0;
            if (tile.path_node.empty()) {
                ++path_nodes.missing;
                continue;
            }
            if (tile.path_node.size() != 1) {
                if (path_nodes.malformed_example.empty()) {
                    path_nodes.malformed_example = fmt::format(
                        "{}.set tile={} value={}", resource.resref.view(),
                        tile_index, tile.path_node);
                }
                ++path_nodes.malformed;
                continue;
            }
            const auto value = static_cast<uint8_t>(tile.path_node.front());
            ++path_nodes.value_counts[value];
            path_nodes.unknown_value
                += !((value >= 'A' && value <= 'Z')
                    || (value >= 'a' && value <= 'p'));
        }
    });

    constexpr std::array labels{"WOK", "PWK", "DWK"};
    std::array<WalkmeshStats, labels.size()> stats{};
    DoorWalkmeshStateStats door_states;
    nw::Vector<float> door_closed_widths;
    nw::Vector<float> walkable_elevation_deltas;
    uint64_t stacked_surface_candidate_resources = 0;
    constexpr std::array recast_cell_sizes{0.25f, 0.125f, 0.0625f};
    constexpr std::array recast_cell_heights{0.10f, 0.05f};
    std::array<nw::nav::NavTileBuildConfig,
        recast_cell_sizes.size() * recast_cell_heights.size()>
        recast_configs{};
    std::array<RecastMergeStats, recast_configs.size()> recast_merges{};
    size_t recast_config_index = 0;
    for (float cell_size : recast_cell_sizes) {
        for (float cell_height : recast_cell_heights) {
            recast_configs[recast_config_index].cell_size = cell_size;
            recast_configs[recast_config_index].cell_height = cell_height;
            // PERSPACE is non-negative and the navigation contract adds 0.1 m.
            // Use the smallest representable actor class for configuration
            // selection; wider classes can only remove additional corridors.
            recast_configs[recast_config_index].erosion_cells
                = static_cast<uint16_t>(std::ceil(0.1f / cell_size));
            ++recast_config_index;
        }
    }
#if defined(ROLLNW_ALL_WALKMESH_RENDERER)
    nw::Vector<nw::Resref> dwk_model_resrefs;
#endif
    std::array<nw::Vector<uint64_t>, labels.size()> surface_counts;
    int64_t elapsed_ns = 0;
    nw::Vector<uint8_t> surface_walkable;
    const auto* surface_table = nw::kernel::twodas().get("surfacemat");
    if (!surface_table
        || nw::nav::build_nav_surface_walkability(
               *surface_table, surface_walkable)
                .walkable_count
            == 0) {
        LOG_F(ERROR, "surfacemat.2da has no walkable rows");
        return 1;
    }

    nw::kernel::resman().visit([&](const nw::Resource& resource) {
        const size_t kind = walkmesh_kind(resource.type);
        if (kind == stats.size()) { return; }

        auto data = nw::kernel::resman().demand(resource);
        auto& current = stats[kind];
        ++current.resources;
        current.bytes += data.bytes.size();
        current.binary_resources += data.bytes.size() != 0 && data.bytes[0] == 0;
#if defined(ROLLNW_ALL_WALKMESH_RENDERER)
        if (kind == walkmesh_kind(nw::ResourceType::dwk)) {
            dwk_model_resrefs.push_back(resource.resref);
        }
#endif

        const auto start = std::chrono::steady_clock::now();
        nw::model::Mdl mdl{std::move(data)};
        const auto stop = std::chrono::steady_clock::now();
        elapsed_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
        if (!mdl.valid()) {
            ++current.invalid;
            return;
        }

        current.nodes += mdl.model.nodes.size();
        uint64_t resource_vertices = 0;
        uint64_t resource_triangles = 0;
        glm::vec3 resource_min{std::numeric_limits<float>::max()};
        glm::vec3 resource_max{std::numeric_limits<float>::lowest()};
        for (const auto& node : mdl.model.nodes) {
            const auto* mesh = dynamic_cast<const nw::model::TrimeshNode*>(node.get());
            if (!mesh) { continue; }

            if (kind == walkmesh_kind(nw::ResourceType::dwk)) {
                nw::String name = mesh->name;
                std::ranges::transform(name, name.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (name.find("closed") != nw::String::npos) {
                    ++door_states.closed_nodes;
                } else if (name.find("open1") != nw::String::npos) {
                    ++door_states.open1_nodes;
                } else if (name.find("open2") != nw::String::npos) {
                    ++door_states.open2_nodes;
                } else if (name.find("open") != nw::String::npos) {
                    ++door_states.other_open_nodes;
                } else {
                    ++door_states.unclassified_nodes;
                    if (door_states.unclassified_examples.size() < 16) {
                        door_states.unclassified_examples.push_back(
                            fmt::format("{}:{}", resource.filename(), mesh->name));
                    }
                }
            }
            current.vertices += mesh->vertices.size();
            current.triangles += mesh->indices.size() / 3;
            resource_vertices += mesh->vertices.size();
            resource_triangles += mesh->indices.size() / 3;
            for (const auto& vertex : mesh->vertices) {
                current.position_min = glm::min(current.position_min, vertex.position);
                current.position_max = glm::max(current.position_max, vertex.position);
                resource_min = glm::min(resource_min, vertex.position);
                resource_max = glm::max(resource_max, vertex.position);
            }
            for (size_t index = 0; index + 2 < mesh->indices.size(); index += 3) {
                const uint16_t ia = mesh->indices[index];
                const uint16_t ib = mesh->indices[index + 1];
                const uint16_t ic = mesh->indices[index + 2];
                if (ia >= mesh->vertices.size() || ib >= mesh->vertices.size()
                    || ic >= mesh->vertices.size()) {
                    continue;
                }
                const glm::vec3 normal = glm::cross(
                    mesh->vertices[ib].position - mesh->vertices[ia].position,
                    mesh->vertices[ic].position - mesh->vertices[ia].position);
                const float length = glm::length(normal);
                if (length <= 1.0e-8f) continue;
                const float vertical_component = std::abs(normal.z / length);
                if (vertical_component >= 0.9f) {
                    ++current.horizontal_triangles;
                } else if (vertical_component <= 0.1f) {
                    ++current.vertical_triangles;
                } else {
                    ++current.angled_triangles;
                }
            }
        }
        if (kind == walkmesh_kind(nw::ResourceType::dwk)) {
            nw::nav::NavGeometry closed_geometry;
            const std::array closed_inputs{nw::nav::NavGeometryModelInput{
                .model = &mdl.model,
                .kind = nw::nav::NavGeometryKind::door_dwk,
                .node_selection = nw::nav::NavGeometryNodeSelection::door_closed,
            }};
            const auto appended = nw::nav::append_nav_geometry_models(
                closed_inputs, closed_geometry);
            if (appended.triangle_count != 0) {
                glm::vec3 minimum{std::numeric_limits<float>::max()};
                glm::vec3 maximum{std::numeric_limits<float>::lowest()};
                for (const auto& vertex : closed_geometry.vertices) {
                    minimum = glm::min(minimum, vertex);
                    maximum = glm::max(maximum, vertex);
                }
                door_closed_widths.push_back(std::max(
                    maximum.x - minimum.x, maximum.y - minimum.y));
            }
        }
        if (kind == walkmesh_kind(nw::ResourceType::wok)
            && resource_triangles != 0) {
            nw::nav::NavGeometry recast_geometry;
            glm::mat4 tile_transform{1.0f};
            tile_transform[3][0] = nw::nav::nav_tile_size * 0.5f;
            tile_transform[3][1] = nw::nav::nav_tile_size * 0.5f;
            const std::array geometry_inputs{nw::nav::NavGeometryModelInput{
                .model = &mdl.model,
                .transform = tile_transform,
                .kind = nw::nav::NavGeometryKind::tile_wok,
            }};
            const auto appended = nw::nav::append_nav_geometry_models(
                geometry_inputs, recast_geometry);
            const uint32_t authored_components = count_authored_components(
                recast_geometry, surface_walkable);
            bool stacked_surface_candidate = false;
            for (size_t triangle = 0;
                triangle < recast_geometry.triangle_count(); ++triangle) {
                const uint32_t surface = recast_geometry.surface[triangle];
                if (surface >= surface_walkable.size()
                    || surface_walkable[surface] == 0) {
                    continue;
                }
                const size_t offset = triangle * 3;
                const auto& a = recast_geometry.vertices[recast_geometry.indices[offset]];
                const auto& b = recast_geometry.vertices[recast_geometry.indices[offset + 1]];
                const auto& c = recast_geometry.vertices[recast_geometry.indices[offset + 2]];
                if (std::abs(glm::cross(b - a, c - a).z) <= 1.0e-8f) continue;
                walkable_elevation_deltas.push_back(
                    std::max({a.z, b.z, c.z}) - std::min({a.z, b.z, c.z}));

                if (!stacked_surface_candidate) {
                    const float min_x = std::min({a.x, b.x, c.x});
                    const float max_x = std::max({a.x, b.x, c.x});
                    const float min_y = std::min({a.y, b.y, c.y});
                    const float max_y = std::max({a.y, b.y, c.y});
                    const float min_z = std::min({a.z, b.z, c.z});
                    const float max_z = std::max({a.z, b.z, c.z});
                    for (size_t other = 0; other < triangle; ++other) {
                        const uint32_t other_surface
                            = recast_geometry.surface[other];
                        if (other_surface >= surface_walkable.size()
                            || surface_walkable[other_surface] == 0) {
                            continue;
                        }
                        const size_t other_offset = other * 3;
                        const auto& d = recast_geometry.vertices[recast_geometry.indices[other_offset]];
                        const auto& e = recast_geometry.vertices[recast_geometry.indices[other_offset + 1]];
                        const auto& f = recast_geometry.vertices[recast_geometry.indices[other_offset + 2]];
                        const float other_min_z = std::min({d.z, e.z, f.z});
                        const float other_max_z = std::max({d.z, e.z, f.z});
                        const bool separated = min_z > other_max_z + 0.5f
                            || other_min_z > max_z + 0.5f;
                        const bool overlaps = max_x >= std::min({d.x, e.x, f.x})
                            && min_x <= std::max({d.x, e.x, f.x})
                            && max_y >= std::min({d.y, e.y, f.y})
                            && min_y <= std::max({d.y, e.y, f.y});
                        if (separated && overlaps) {
                            stacked_surface_candidate = true;
                            break;
                        }
                    }
                }
            }
            stacked_surface_candidate_resources += stacked_surface_candidate;
            nw::Vector<uint32_t> candidates(recast_geometry.triangle_count());
            for (size_t triangle = 0; triangle < candidates.size(); ++triangle) {
                candidates[triangle] = static_cast<uint32_t>(triangle);
            }
            const nw::nav::NavTileBuildInput input{
                .surface_vertices = recast_geometry.vertices,
                .surface_indices = recast_geometry.indices,
                .surface_ids = recast_geometry.surface,
                .surface_triangles = candidates,
                .surface_walkable = surface_walkable,
                .tile = {0, 0},
            };
            bool has_walkable_input = false;
            for (size_t triangle = 0;
                triangle < recast_geometry.triangle_count(); ++triangle) {
                const uint32_t surface = recast_geometry.surface[triangle];
                const size_t offset = triangle * 3;
                const auto& a = recast_geometry.vertices[recast_geometry.indices[offset]];
                const auto& b = recast_geometry.vertices[recast_geometry.indices[offset + 1]];
                const auto& c = recast_geometry.vertices[recast_geometry.indices[offset + 2]];
                if (surface < surface_walkable.size()
                    && surface_walkable[surface] != 0
                    && std::abs(glm::cross(b - a, c - a).z) > 1.0e-8f) {
                    has_walkable_input = true;
                    break;
                }
            }
            for (size_t config_index = 0;
                config_index < recast_configs.size(); ++config_index) {
                auto& recast_merge = recast_merges[config_index];
                ++recast_merge.resources;
                recast_merge.no_walkable_input += !has_walkable_input;
                nw::nav::NavTileData tile_data;
                const auto recast_start = std::chrono::steady_clock::now();
                const auto merge = nw::nav::build_nav_tile_data(
                    input, recast_configs[config_index], tile_data);
                const auto recast_stop = std::chrono::steady_clock::now();
                recast_merge.elapsed_ns
                    += std::chrono::duration_cast<std::chrono::nanoseconds>(
                        recast_stop - recast_start)
                           .count();
                if (appended.rejected_input_count != 0
                    || appended.rejected_mesh_count != 0
                    || merge.status != nw::nav::NavStatus::ok) {
                    ++recast_merge.rejected;
                } else if (merge.polygon_count == 0) {
                    ++recast_merge.empty;
                    if (has_walkable_input) {
                        ++recast_merge.lost_walkable_input;
                        if (recast_merge.lost_walkable_examples.size() < 16) {
                            recast_merge.lost_walkable_examples.push_back(
                                resource.filename());
                        }
                    }
                } else {
                    ++recast_merge.built;
                    recast_merge.input_triangles
                        += merge.input_surface_triangle_count;
                    recast_merge.merged_polygons += merge.polygon_count;
                    recast_merge.merged_vertices += merge.vertex_count;
                    recast_merge.detail_triangles += merge.detail_triangle_count;
                    recast_merge.payload_bytes += merge.payload_bytes;
                    measure_surface_error(recast_geometry, surface_walkable,
                        recast_configs[config_index], tile_data,
                        authored_components, resource.filename(),
                        recast_merge);
                    if (merge.input_surface_triangle_count
                        > recast_merge.worst_resource_triangles) {
                        recast_merge.worst_resource_triangles
                            = merge.input_surface_triangle_count;
                        recast_merge.worst_resource_polygons
                            = merge.polygon_count;
                        recast_merge.worst_resource = resource.filename();
                    }
                }
            }
        }
        current.empty += resource_triangles == 0;
        if (resource_vertices > current.maximum_vertices) {
            current.maximum_vertices = resource_vertices;
            current.maximum_vertices_resource = resource.filename();
        }
        if (resource_triangles > current.maximum_triangles) {
            current.maximum_triangles = resource_triangles;
            current.maximum_triangles_resource = resource.filename();
        }
        if (resource_vertices != 0) {
            const float horizontal_span = std::max(
                resource_max.x - resource_min.x,
                resource_max.y - resource_min.y);
            if (horizontal_span > current.maximum_horizontal_span) {
                current.maximum_horizontal_span = horizontal_span;
                current.maximum_horizontal_span_resource = resource.filename();
            }
        }
        for (const uint32_t surface : mdl.model.face_materials) {
            if (surface_counts[kind].size() <= surface) {
                surface_counts[kind].resize(static_cast<size_t>(surface) + 1);
            }
            ++surface_counts[kind][surface];
        }
    });

    uint64_t invalid = 0;
    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& current = stats[i];
        invalid += current.invalid;
        LOG_F(INFO,
            "{} resources={} binary={} invalid={} empty={} bytes={} nodes={} vertices={} triangles={} min=({}, {}, {}) max=({}, {}, {})",
            labels[i], current.resources, current.binary_resources, current.invalid, current.empty, current.bytes,
            current.nodes, current.vertices, current.triangles,
            current.position_min.x, current.position_min.y, current.position_min.z,
            current.position_max.x, current.position_max.y, current.position_max.z);
        LOG_F(INFO,
            "{} maximum_vertices={} resource={} maximum_triangles={} resource={} maximum_horizontal_span={} resource={}",
            labels[i], current.maximum_vertices, current.maximum_vertices_resource,
            current.maximum_triangles, current.maximum_triangles_resource,
            current.maximum_horizontal_span, current.maximum_horizontal_span_resource);
        LOG_F(INFO, "{} triangle orientation horizontal={} vertical={} angled={}",
            labels[i], current.horizontal_triangles, current.vertical_triangles,
            current.angled_triangles);
    }
    for (size_t kind = 0; kind < surface_counts.size(); ++kind) {
        for (size_t surface = 0; surface < surface_counts[kind].size(); ++surface) {
            if (surface_counts[kind][surface] == 0) continue;
            nw::StringView label;
            int32_t walkable = -1;
            if (surface_table && surface < surface_table->rows()) {
                (void)surface_table->get_to(surface, "Label", label, false);
                (void)surface_table->get_to(surface, "Walk", walkable, false);
            }
            LOG_F(INFO, "{} surface={} label={} walkable={} triangles={}",
                labels[kind], surface, label, walkable,
                surface_counts[kind][surface]);
        }
    }
    LOG_F(INFO,
        "DWK mesh state names closed={} open1={} open2={} other_open={} unclassified={}",
        door_states.closed_nodes, door_states.open1_nodes, door_states.open2_nodes,
        door_states.other_open_nodes, door_states.unclassified_nodes);
    for (const auto& example : door_states.unclassified_examples) {
        LOG_F(INFO, "DWK unclassified mesh example={}", example);
    }
    LOG_F(INFO,
        "DWK closed footprint widths count={} min={} p50={} p95={} max={}",
        door_closed_widths.size(), percentile(door_closed_widths, 0.0f),
        percentile(door_closed_widths, 0.5f),
        percentile(door_closed_widths, 0.95f),
        percentile(door_closed_widths, 1.0f));
    LOG_F(INFO,
        "WOK walkable triangle elevation_delta count={} p50={} p95={} p99={} max={} stacked_surface_candidate_resources={}",
        walkable_elevation_deltas.size(),
        percentile(walkable_elevation_deltas, 0.5f),
        percentile(walkable_elevation_deltas, 0.95f),
        percentile(walkable_elevation_deltas, 0.99f),
        percentile(walkable_elevation_deltas, 1.0f),
        stacked_surface_candidate_resources);
    LOG_F(INFO,
        "SET tile path nodes tilesets={} tiles={} missing={} malformed={} unknown_value={} non_quarter_turn_orientation={}",
        path_nodes.tilesets, path_nodes.tiles, path_nodes.missing,
        path_nodes.malformed, path_nodes.unknown_value,
        path_nodes.non_quarter_turn_orientation);
    if (!path_nodes.malformed_example.empty()) {
        LOG_F(INFO, "SET tile path node malformed example={}",
            path_nodes.malformed_example);
    }
    for (size_t value = 0; value < path_nodes.value_counts.size(); ++value) {
        if (path_nodes.value_counts[value] == 0) continue;
        LOG_F(INFO, "SET tile path node value={} byte={} tiles={}",
            static_cast<char>(value), value, path_nodes.value_counts[value]);
    }
    for (const auto& [orientation, count] : path_nodes.orientation_counts) {
        LOG_F(INFO, "SET tile path node orientation={} tiles={}",
            orientation, count);
    }

    for (size_t config_index = 0;
        config_index < recast_configs.size(); ++config_index) {
        const auto& config = recast_configs[config_index];
        const auto& recast_merge = recast_merges[config_index];
        LOG_F(INFO,
            "WOK Recast config cell_size={} cell_height={} erosion_cells={} resources={} built={} rejected={} empty={} no_walkable_input={} lost_walkable_input={} input_triangles={} polygons={} vertices={} detail_triangles={} payload_bytes={} authored_components={} recast_components={} component_splits={} component_joins={} height_samples={} height_projection_failures={} height_error_violations={} maximum_height_error={} build_ms={:.1f}",
            config.cell_size, config.cell_height, config.erosion_cells,
            recast_merge.resources,
            recast_merge.built, recast_merge.rejected, recast_merge.empty,
            recast_merge.no_walkable_input,
            recast_merge.lost_walkable_input,
            recast_merge.input_triangles, recast_merge.merged_polygons,
            recast_merge.merged_vertices, recast_merge.detail_triangles,
            recast_merge.payload_bytes,
            recast_merge.authored_components,
            recast_merge.recast_components,
            recast_merge.component_split_resources,
            recast_merge.component_join_resources,
            recast_merge.height_samples,
            recast_merge.height_projection_failures,
            recast_merge.height_error_violations,
            recast_merge.maximum_height_error,
            static_cast<double>(recast_merge.elapsed_ns) / 1.0e6);
        if (recast_merge.merged_polygons != 0) {
            LOG_F(INFO,
                "WOK Recast config cell_size={} cell_height={} triangles_per_polygon={:.3f} largest_resource={} triangles={} polygons={}",
                config.cell_size, config.cell_height,
                static_cast<double>(recast_merge.input_triangles)
                    / static_cast<double>(recast_merge.merged_polygons),
                recast_merge.worst_resource,
                recast_merge.worst_resource_triangles,
                recast_merge.worst_resource_polygons);
        }
        for (const auto& example : recast_merge.lost_walkable_examples) {
            LOG_F(INFO,
                "WOK Recast lost walkable cell_size={} cell_height={} resource={}",
                config.cell_size, config.cell_height, example);
        }
        for (const auto& example : recast_merge.component_mismatch_examples) {
            LOG_F(INFO,
                "WOK Recast component mismatch cell_size={} cell_height={} {}",
                config.cell_size, config.cell_height, example);
        }
        LOG_F(INFO,
            "WOK Recast maximum height error cell_size={} cell_height={} {}",
            config.cell_size, config.cell_height,
            recast_merge.maximum_height_error_sample);
    }

#if defined(ROLLNW_ALL_WALKMESH_RENDERER)
    DoorModelAnimationStats door_models;
    nw::Vector<nw::Resref> door_model_resrefs;
    append_door_models(nw::kernel::twodas().get("doortypes"),
        "Model", door_model_resrefs, door_models);
    append_door_models(nw::kernel::twodas().get("genericdoors"),
        "ModelName", door_model_resrefs, door_models);
    std::sort(door_model_resrefs.begin(), door_model_resrefs.end());
    door_model_resrefs.erase(
        std::unique(door_model_resrefs.begin(), door_model_resrefs.end()),
        door_model_resrefs.end());
    door_models.unique_models = door_model_resrefs.size();
    std::sort(dwk_model_resrefs.begin(), dwk_model_resrefs.end());
    dwk_model_resrefs.erase(
        std::unique(dwk_model_resrefs.begin(), dwk_model_resrefs.end()),
        dwk_model_resrefs.end());
    for (const auto model_resref : door_model_resrefs) {
        door_models.missing_dwks += !std::binary_search(
            dwk_model_resrefs.begin(), dwk_model_resrefs.end(), model_resref);
        const nw::Resource resource{model_resref, nw::ResourceType::mdl};
        if (!nw::kernel::resman().contains(resource)) {
            ++door_models.missing_models;
            continue;
        }
        auto data = nw::kernel::resman().demand(resource);
        if (data.bytes.size() == 0) {
            ++door_models.unavailable_model_payloads;
            continue;
        }
        nw::model::Mdl mdl{std::move(data)};
        if (!mdl.valid()) {
            ++door_models.invalid_models;
            continue;
        }
        door_models.maximum_horizontal_extent = std::max(
            door_models.maximum_horizontal_extent,
            std::max(mdl.model.bmax.x - mdl.model.bmin.x,
                mdl.model.bmax.y - mdl.model.bmin.y));
        door_models.maximum_vertical_extent = std::max(
            door_models.maximum_vertical_extent,
            mdl.model.bmax.z - mdl.model.bmin.z);

        auto imported = nw::render::nwn::import_nwn_model_asset(mdl);
        if (!imported.asset) {
            ++door_models.invalid_models;
            continue;
        }
        nw::render::RenderModel runtime_model;
        runtime_model.name = imported.asset->name;
        runtime_model.skeletons = imported.asset->skeletons;
        runtime_model.animations = imported.asset->animations;
        auto backend = nw::render::make_render_model_animation_backend(
            runtime_model);
        nw::render::Pose pose;
        for (uint32_t clip_index = 0;
            clip_index < runtime_model.animations.size(); ++clip_index) {
            const auto& clip = runtime_model.animations[clip_index];
            if (!door_animation_family(clip.name)) continue;
            ++door_models.declared_family_clips;
            door_models.zero_duration_family_clips += clip.duration <= 0.0f;
            if (door_hold_animation(clip.name)) {
                ++door_models.declared_hold_clips;
                door_models.zero_duration_hold_clips += clip.duration <= 0.0f;
            }
            if (backend
                && backend->sample(clip_index, 0.0f, pose, false)) {
                ++door_models.backend_sampleable_family_clips;
            }
        }
    }
    LOG_F(INFO,
        "door models rows={} unique={} missing_mdl={} missing_dwk={} unavailable_mdl_payload={} invalid={} declared_family_clips={} backend_sampleable_family_clips={} zero_duration_family_clips={} declared_hold_clips={} zero_duration_hold_clips={} maximum_horizontal_extent={} maximum_vertical_extent={}",
        door_models.model_rows, door_models.unique_models,
        door_models.missing_models, door_models.missing_dwks,
        door_models.unavailable_model_payloads, door_models.invalid_models,
        door_models.declared_family_clips,
        door_models.backend_sampleable_family_clips,
        door_models.zero_duration_family_clips,
        door_models.declared_hold_clips,
        door_models.zero_duration_hold_clips,
        door_models.maximum_horizontal_extent,
        door_models.maximum_vertical_extent);
#else
    LOG_F(INFO, "door model animation audit skipped: renderer disabled");
#endif
    LOG_F(INFO, "walkmesh parse time={}ns", elapsed_ns);

    uint64_t tileset_tiles = 0;
    uint64_t tileset_tiles_without_wok = 0;
    nw::Vector<nw::String> missing_wok_examples;
    for (const auto& [tileset_name, tileset] : nw::kernel::tilesets().tileset_map_) {
        for (const auto& tile : tileset.tiles) {
            ++tileset_tiles;
            if (nw::kernel::resman().contains(
                    {nw::Resref{tile.model}, nw::ResourceType::wok})) {
                continue;
            }
            ++tileset_tiles_without_wok;
            if (missing_wok_examples.size() < 16) {
                missing_wok_examples.push_back(
                    fmt::format("{}:{}", tileset_name, tile.model));
            }
        }
    }
    LOG_F(INFO, "tilesets={} tile_definitions={} missing_wok={}",
        nw::kernel::tilesets().tileset_map_.size(), tileset_tiles,
        tileset_tiles_without_wok);
    for (const auto& example : missing_wok_examples) {
        LOG_F(INFO, "missing WOK tile example={}", example);
    }

    const auto* appearances = nw::kernel::twodas().get("appearance");
    if (!appearances) {
        LOG_F(ERROR, "appearance.2da unavailable");
        return 1;
    }
    nw::Vector<float> per_space;
    uint64_t missing_per_space = 0;
    per_space.reserve(appearances->rows());
    for (size_t row = 0; row < appearances->rows(); ++row) {
        float radius = 0.0f;
        if (!appearances->get_to(row, "PERSPACE", radius, false)
            || !std::isfinite(radius) || radius < 0.0f) {
            ++missing_per_space;
            continue;
        }
        per_space.push_back(radius);
    }
    std::sort(per_space.begin(), per_space.end());
    LOG_F(INFO, "appearance PERSPACE rows={} valid={} missing={}",
        appearances->rows(), per_space.size(), missing_per_space);
    for (size_t begin = 0; begin < per_space.size();) {
        size_t end = begin + 1;
        while (end < per_space.size()
            && per_space[end] == per_space[begin]) {
            ++end;
        }
        LOG_F(INFO, "appearance PERSPACE value={} count={}",
            per_space[begin], end - begin);
        begin = end;
    }
    for (float cell_size : recast_cell_sizes) {
        nw::Vector<uint16_t> radius_cells;
        radius_cells.reserve(per_space.size());
        for (float radius : per_space) {
            radius_cells.push_back(static_cast<uint16_t>(std::ceil(
                static_cast<double>(radius + 0.1f)
                / static_cast<double>(cell_size))));
        }
        nw::Vector<uint16_t> classes = radius_cells;
        std::sort(classes.begin(), classes.end());
        classes.erase(std::unique(classes.begin(), classes.end()),
            classes.end());
        LOG_F(INFO,
            "appearance radius classes cell_size={} classes={}",
            cell_size, classes.size());
        for (size_t class_index = 0;
            class_index < classes.size(); ++class_index) {
            LOG_F(INFO,
                "appearance radius class cell_size={} class={} erosion_cells={} eroded_radius={} rows={}",
                cell_size, class_index, classes[class_index],
                static_cast<float>(classes[class_index]) * cell_size,
                std::ranges::count(radius_cells, classes[class_index]));
        }
    }

    const auto* creature_speed = nw::kernel::twodas().get("creaturespeed");
    if (!creature_speed) {
        LOG_F(ERROR, "creaturespeed.2da unavailable");
        return 1;
    }
    uint64_t valid_walk_rates = 0;
    uint64_t missing_walk_rates = 0;
    float minimum_walk_rate = std::numeric_limits<float>::max();
    float maximum_walk_rate = std::numeric_limits<float>::lowest();
    for (size_t row = 0; row < creature_speed->rows(); ++row) {
        float walk_rate = 0.0f;
        if (!creature_speed->get_to(row, "WALKRATE", walk_rate, false)
            || !std::isfinite(walk_rate)) {
            ++missing_walk_rates;
            continue;
        }
        ++valid_walk_rates;
        minimum_walk_rate = std::min(minimum_walk_rate, walk_rate);
        maximum_walk_rate = std::max(maximum_walk_rate, walk_rate);
        LOG_F(INFO, "creaturespeed row={} walkrate={}", row, walk_rate);
    }
    LOG_F(INFO,
        "creaturespeed rows={} valid_walkrate={} missing_walkrate={} minimum={} maximum={}",
        creature_speed->rows(), valid_walk_rates, missing_walk_rates,
        minimum_walk_rate, maximum_walk_rate);
    return invalid == 0 ? 0 : 1;
}
