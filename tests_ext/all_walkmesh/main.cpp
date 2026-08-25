#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

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

} // namespace

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    if (!nw::test::configure_dedicated_server("test_data/user/")) { return 1; }
    nw::kernel::config().initialize({true, false});
    nw::kernel::services().start();

    constexpr std::array labels{"WOK", "PWK", "DWK"};
    std::array<WalkmeshStats, labels.size()> stats{};
    DoorWalkmeshStateStats door_states;
    nw::Vector<uint64_t> surface_counts;
    int64_t elapsed_ns = 0;

    nw::kernel::resman().visit([&](const nw::Resource& resource) {
        const size_t kind = walkmesh_kind(resource.type);
        if (kind == stats.size()) { return; }

        auto data = nw::kernel::resman().demand(resource);
        auto& current = stats[kind];
        ++current.resources;
        current.bytes += data.bytes.size();
        current.binary_resources += data.bytes.size() != 0 && data.bytes[0] == 0;

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
            if (surface_counts.size() <= surface) {
                surface_counts.resize(static_cast<size_t>(surface) + 1);
            }
            ++surface_counts[surface];
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
    for (size_t surface = 0; surface < surface_counts.size(); ++surface) {
        if (surface_counts[surface] != 0) {
            LOG_F(INFO, "surface {} triangles={}", surface, surface_counts[surface]);
        }
    }
    LOG_F(INFO,
        "DWK mesh state names closed={} open1={} open2={} other_open={} unclassified={}",
        door_states.closed_nodes, door_states.open1_nodes, door_states.open2_nodes,
        door_states.other_open_nodes, door_states.unclassified_nodes);
    for (const auto& example : door_states.unclassified_examples) {
        LOG_F(INFO, "DWK unclassified mesh example={}", example);
    }
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
