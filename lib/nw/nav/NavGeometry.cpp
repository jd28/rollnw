#include "NavGeometry.hpp"

#include "../formats/StaticTwoDA.hpp"
#include "../formats/Tileset.hpp"
#include "../model/Mdl.hpp"
#include "../objects/Area.hpp"
#include "../objects/AreaTransforms.hpp"
#include "../objects/Door.hpp"
#include "../objects/ObjectManager.hpp"
#include "../objects/Placeable.hpp"
#include "../resources/ResourceManager.hpp"
#include "../util/string.hpp"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <limits>

#include <absl/container/flat_hash_map.h>

namespace nw::nav {
namespace {

bool finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const glm::mat4& value)
{
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) return false;
        }
    }
    return true;
}

bool mesh_selected(const model::TrimeshNode& mesh, NavGeometryNodeSelection selection)
{
    if (selection == NavGeometryNodeSelection::all) return true;
    String name = mesh.name;
    string::tolower(&name);
    return name.find("closed") != String::npos;
}

const ObjectVisualModel* root_visual_model(const ObjectBase& object)
{
    const auto* visual = kernel::objects().components().find_visual(object.handle());
    if (!visual) return nullptr;
    auto found = std::find_if(visual->models.begin(), visual->models.end(), [](const ObjectVisualModel& model) {
        return model.kind == ObjectVisualModelKind::root
            && !model.model.empty()
            && object_visual_model_visible_in_mode(model, ObjectVisualRenderMode::game);
    });
    return found != visual->models.end() ? &*found : nullptr;
}

glm::mat4 node_local_transform(const model::Node& node)
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    const auto position_value = node.get_controller(model::ControllerType::Position, false);
    if (position_value.data.size() >= 3) {
        position = {position_value.data[0], position_value.data[1], position_value.data[2]};
    }

    const auto rotation_value = node.get_controller(model::ControllerType::Orientation, false);
    if (rotation_value.data.size() >= 4) {
        const glm::quat parsed{
            rotation_value.data[3],
            rotation_value.data[0],
            rotation_value.data[1],
            rotation_value.data[2],
        };
        if (glm::dot(parsed, parsed) >= 1.0e-12f) rotation = glm::normalize(parsed);
    }

    const auto scale_value = node.get_controller(model::ControllerType::Scale, false);
    if (scale_value.data.size() >= 3) {
        scale = {scale_value.data[0], scale_value.data[1], scale_value.data[2]};
    } else if (!scale_value.data.empty()) {
        scale = glm::vec3{scale_value.data[0]};
    }

    return glm::scale(
        glm::translate(glm::mat4{1.0f}, position) * glm::toMat4(rotation),
        scale);
}

bool node_model_transform(const model::Node& node, size_t maximum_depth, glm::mat4& output)
{
    // Model nodes already express their cold parent graph with stable pointers.
    // Converting this sparse sidecar to indices would add a lookup table solely
    // for extraction; the resulting transform is flattened before the hot path.
    std::array<const model::Node*, 64> inline_chain{};
    Vector<const model::Node*> overflow_chain;
    size_t count = 0;

    for (const model::Node* current = &node; current; current = current->parent) {
        if (count >= maximum_depth) return false;
        if (count < inline_chain.size()) {
            inline_chain[count] = current;
        } else {
            overflow_chain.push_back(current);
        }
        ++count;
    }

    output = glm::mat4{1.0f};
    while (count > 0) {
        --count;
        const model::Node* current = count < inline_chain.size()
            ? inline_chain[count]
            : overflow_chain[count - inline_chain.size()];
        output *= node_local_transform(*current);
    }
    return finite(output);
}

struct QuantizedPoint {
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    auto operator<=>(const QuantizedPoint&) const = default;
};

struct EdgeRecord {
    QuantizedPoint first;
    QuantizedPoint second;
    uint32_t triangle = 0;
    uint8_t edge = 0;

    auto operator<=>(const EdgeRecord& other) const
    {
        if (auto result = first <=> other.first; result != 0) return result;
        if (auto result = second <=> other.second; result != 0) return result;
        if (auto result = triangle <=> other.triangle; result != 0) return result;
        return edge <=> other.edge;
    }
};

bool quantize(const glm::vec3& value, double inverse_step, QuantizedPoint& output)
{
    if (!finite(value)) return false;
    constexpr double limit = static_cast<double>(std::numeric_limits<int64_t>::max());
    const std::array<double, 3> scaled{
        std::round(static_cast<double>(value.x) * inverse_step),
        std::round(static_cast<double>(value.y) * inverse_step),
        std::round(static_cast<double>(value.z) * inverse_step),
    };
    if (std::abs(scaled[0]) > limit || std::abs(scaled[1]) > limit || std::abs(scaled[2]) > limit) {
        return false;
    }
    output = {
        static_cast<int64_t>(scaled[0]),
        static_cast<int64_t>(scaled[1]),
        static_cast<int64_t>(scaled[2]),
    };
    return true;
}

} // namespace

void NavGeometry::clear()
{
    vertices.clear();
    indices.clear();
    surface.clear();
    kind.clear();
    owner.clear();
    adjacency.clear();
}

bool NavGeometry::valid() const noexcept
{
    const size_t triangles = triangle_count();
    return indices.size() % 3 == 0
        && surface.size() == triangles
        && kind.size() == triangles
        && owner.size() == triangles
        && (adjacency.empty() || adjacency.size() == indices.size());
}

NavGeometryAppendStats append_nav_geometry_models(
    std::span<const NavGeometryModelInput> inputs,
    NavGeometry& output)
{
    NavGeometryAppendStats stats;
    stats.input_count = inputs.size();

    for (const auto& input : inputs) {
        if (!input.model || !finite(input.transform)) {
            ++stats.rejected_input_count;
            continue;
        }

        const auto& model = *input.model;
        for (const auto& range : model.face_material_ranges) {
            if (range.node_index >= model.nodes.size()
                || range.material_offset > model.face_materials.size()
                || range.face_count > model.face_materials.size() - range.material_offset) {
                ++stats.rejected_mesh_count;
                continue;
            }

            const auto* mesh = dynamic_cast<const model::TrimeshNode*>(model.nodes[range.node_index].get());
            if (!mesh || mesh->indices.size() / 3 != range.face_count) {
                ++stats.rejected_mesh_count;
                continue;
            }
            if (!mesh_selected(*mesh, input.node_selection)) continue;

            glm::mat4 node_transform{1.0f};
            if (!node_model_transform(*mesh, model.nodes.size() + 1, node_transform)
                || !finite(input.transform * node_transform)
                || output.vertices.size() > std::numeric_limits<uint32_t>::max() - mesh->vertices.size()) {
                ++stats.rejected_mesh_count;
                continue;
            }

            const glm::mat4 transform = input.transform * node_transform;
            Vector<uint32_t> remap(mesh->vertices.size(), std::numeric_limits<uint32_t>::max());
            for (size_t vertex_index = 0; vertex_index < mesh->vertices.size(); ++vertex_index) {
                const glm::vec4 transformed = transform * glm::vec4{mesh->vertices[vertex_index].position, 1.0f};
                const glm::vec3 position{transformed};
                if (!finite(position)) continue;
                remap[vertex_index] = static_cast<uint32_t>(output.vertices.size());
                output.vertices.push_back(position);
            }

            for (size_t face = 0; face < range.face_count; ++face) {
                const size_t index_offset = face * 3;
                const uint16_t source_a = mesh->indices[index_offset];
                const uint16_t source_b = mesh->indices[index_offset + 1];
                const uint16_t source_c = mesh->indices[index_offset + 2];
                if (source_a >= remap.size() || source_b >= remap.size() || source_c >= remap.size()
                    || remap[source_a] == std::numeric_limits<uint32_t>::max()
                    || remap[source_b] == std::numeric_limits<uint32_t>::max()
                    || remap[source_c] == std::numeric_limits<uint32_t>::max()) {
                    ++stats.dropped_triangle_count;
                    continue;
                }

                output.indices.push_back(remap[source_a]);
                output.indices.push_back(remap[source_b]);
                output.indices.push_back(remap[source_c]);
                output.surface.push_back(model.face_materials[range.material_offset + face]);
                output.kind.push_back(input.kind);
                output.owner.push_back(input.owner);
                ++stats.triangle_count;
            }
            ++stats.mesh_count;
        }
    }

    output.adjacency.clear();
    return stats;
}

NavAdjacencyStats build_nav_geometry_adjacency(
    NavGeometry& geometry,
    float quantization_step)
{
    NavAdjacencyStats stats;
    stats.triangle_count = geometry.triangle_count();
    geometry.adjacency.assign(geometry.indices.size(), -1);

    if (!geometry.valid() || !std::isfinite(quantization_step) || quantization_step <= 0.0f) {
        stats.rejected_edge_count = geometry.indices.size();
        return stats;
    }

    const double inverse_step = 1.0 / static_cast<double>(quantization_step);
    Vector<EdgeRecord> edges;
    edges.reserve(geometry.indices.size());

    for (size_t triangle = 0; triangle < stats.triangle_count; ++triangle) {
        const size_t index_offset = triangle * 3;
        for (uint8_t edge = 0; edge < 3; ++edge) {
            const uint32_t a = geometry.indices[index_offset + edge];
            const uint32_t b = geometry.indices[index_offset + ((edge + 1) % 3)];
            if (a >= geometry.vertices.size() || b >= geometry.vertices.size()) {
                ++stats.rejected_edge_count;
                continue;
            }

            QuantizedPoint first;
            QuantizedPoint second;
            if (!quantize(geometry.vertices[a], inverse_step, first)
                || !quantize(geometry.vertices[b], inverse_step, second)) {
                ++stats.rejected_edge_count;
                continue;
            }
            if (second < first) std::swap(first, second);
            edges.push_back({first, second, static_cast<uint32_t>(triangle), edge});
        }
    }

    std::sort(edges.begin(), edges.end());
    for (size_t begin = 0; begin < edges.size();) {
        size_t end = begin + 1;
        while (end < edges.size()
            && edges[end].first == edges[begin].first
            && edges[end].second == edges[begin].second) {
            ++end;
        }

        const size_t run_size = end - begin;
        if (run_size == 2 && edges[begin].triangle != edges[begin + 1].triangle) {
            const auto& first = edges[begin];
            const auto& second = edges[begin + 1];
            geometry.adjacency[static_cast<size_t>(first.triangle) * 3 + first.edge]
                = static_cast<int32_t>(second.triangle);
            geometry.adjacency[static_cast<size_t>(second.triangle) * 3 + second.edge]
                = static_cast<int32_t>(first.triangle);
            stats.linked_edge_count += 2;
        } else if (run_size > 2) {
            stats.non_manifold_edge_count += run_size;
        }
        begin = end;
    }

    stats.boundary_edge_count = geometry.indices.size()
        - stats.linked_edge_count
        - stats.rejected_edge_count;
    return stats;
}

NavAreaGeometryStats build_area_tile_nav_geometry(
    const Area& area,
    const ResourceManager& resources,
    NavGeometry& output)
{
    NavAreaGeometryStats stats;
    output.clear();
    if (!area.tileset || area.width <= 0 || area.height <= 0
        || !std::isfinite(area.tileset->tile_height)
        || area.tileset->tile_height <= 0.0f
        || static_cast<size_t>(area.height) > std::numeric_limits<size_t>::max() / static_cast<size_t>(area.width)) {
        return stats;
    }

    struct TileRow {
        Resource resource;
        AreaTileTransformInput transform;
        NavGeometryKind kind = NavGeometryKind::tile_wok;
        uint32_t owner = 0;
    };

    Vector<TileRow> tile_rows;
    const size_t declared_tile_count = static_cast<size_t>(area.width) * static_cast<size_t>(area.height);
    tile_rows.reserve(std::min(declared_tile_count, area.tiles.size()));
    stats.tile_count = std::min(declared_tile_count, area.tiles.size());

    for (int32_t y = 0; y < area.height; ++y) {
        for (int32_t x = 0; x < area.width; ++x) {
            const size_t tile_index = static_cast<size_t>(y) * static_cast<size_t>(area.width)
                + static_cast<size_t>(x);
            if (tile_index >= area.tiles.size()
                || tile_index > std::numeric_limits<uint32_t>::max()) {
                ++stats.rejected_tile_count;
                continue;
            }

            const auto& tile = area.tiles[tile_index];
            if (tile.id < 0 || static_cast<size_t>(tile.id) >= area.tileset->tiles.size()) {
                ++stats.rejected_tile_count;
                continue;
            }
            const auto& definition = area.tileset->tiles[static_cast<size_t>(tile.id)];
            if (definition.model.empty()) {
                ++stats.rejected_tile_count;
                continue;
            }

            const Resource wok{definition.model, ResourceType::wok};
            const AreaTileTransformInput transform{x, y, tile.height, tile.orientation};
            if (tile.orientation < 0 || tile.orientation >= 4) {
                ++stats.rejected_tile_count;
                continue;
            }
            if (resources.contains(wok)) {
                tile_rows.push_back({wok, transform, NavGeometryKind::tile_wok, static_cast<uint32_t>(tile_index)});
                ++stats.wok_tile_count;
                continue;
            }

            const Resource mdl{definition.model, ResourceType::mdl};
            if (!resources.contains(mdl)) {
                ++stats.missing_tile_count;
                continue;
            }
            tile_rows.push_back({mdl, transform, NavGeometryKind::tile_aabb, static_cast<uint32_t>(tile_index)});
            ++stats.fallback_tile_count;
        }
    }

    Vector<AreaTileTransformInput> transform_inputs;
    Vector<glm::mat4> transforms(tile_rows.size(), glm::mat4{1.0f});
    transform_inputs.reserve(tile_rows.size());
    for (const auto& row : tile_rows)
        transform_inputs.push_back(row.transform);
    const auto transform_stats = build_area_tile_world_transforms(
        area.tileset->tile_height, transform_inputs, transforms);
    stats.rejected_tile_count += transform_stats.rejected_count;

    struct CachedModel {
        std::unique_ptr<model::Mdl> model;
        bool valid = false;
        bool empty = false;
    };
    Vector<CachedModel> cached_models;
    absl::flat_hash_map<Resource, size_t> resource_indices;
    resource_indices.reserve(tile_rows.size());
    Vector<NavGeometryModelInput> append_inputs;
    append_inputs.reserve(tile_rows.size());

    for (size_t i = 0; i < tile_rows.size(); ++i) {
        const auto [iterator, inserted] = resource_indices.try_emplace(
            tile_rows[i].resource, cached_models.size());
        if (inserted) {
            auto data = resources.demand(tile_rows[i].resource);
            if (data.bytes.size() == 0) {
                cached_models.push_back({nullptr, false, true});
            } else {
                auto parsed = std::make_unique<model::Mdl>(std::move(data));
                const bool valid = parsed->valid();
                cached_models.push_back({std::move(parsed), valid, false});
            }
        }
        const auto& cached = cached_models[iterator->second];
        if (cached.empty) {
            if (tile_rows[i].resource.type == ResourceType::wok) {
                ++stats.empty_wok_tile_count;
            } else {
                ++stats.rejected_tile_count;
            }
            continue;
        }
        if (!cached.valid) {
            ++stats.rejected_tile_count;
            continue;
        }
        append_inputs.push_back({
            .model = &cached.model->model,
            .transform = transforms[i],
            .kind = tile_rows[i].kind,
            .owner = tile_rows[i].owner,
        });
    }

    stats.unique_resource_count = cached_models.size();
    stats.append = append_nav_geometry_models(append_inputs, output);
    stats.adjacency = build_nav_geometry_adjacency(output);
    return stats;
}

NavObjectGeometryStats build_area_object_nav_geometry(
    const Area& area,
    const ResourceManager& resources,
    NavGeometry& output)
{
    NavObjectGeometryStats stats;
    output.clear();

    struct ObjectRow {
        Resource resource;
        AreaObjectTransformInput transform;
        NavGeometryKind kind = NavGeometryKind::placeable_pwk;
        NavGeometryNodeSelection node_selection = NavGeometryNodeSelection::all;
        uint32_t owner = 0;
    };

    Vector<ObjectRow> object_rows;
    object_rows.reserve(area.placeables.size() + area.doors.size());
    auto append_object = [&](ObjectBase* object, NavGeometryKind kind, ResourceType::type resource_type,
                             NavGeometryNodeSelection selection) {
        if (!object || object->handle().id == object_invalid || !object->instantiate()) {
            ++stats.rejected_object_count;
            return;
        }
        const auto* visual = root_visual_model(*object);
        if (!visual) {
            ++stats.missing_visual_count;
            return;
        }
        const Resource resource{visual->model, resource_type};
        if (!resources.contains(resource)) {
            ++stats.missing_walkmesh_count;
            return;
        }
        const auto* spatial = kernel::objects().components().find_spatial(object->handle());
        if (!spatial) {
            ++stats.rejected_object_count;
            return;
        }
        object_rows.push_back({
            .resource = resource,
            .transform = {spatial->position, spatial->orientation, spatial->scale},
            .kind = kind,
            .node_selection = selection,
            .owner = static_cast<uint32_t>(object->handle().id),
        });
    };

    stats.placeable_count = area.placeables.size();
    for (auto* placeable : area.placeables) {
        append_object(placeable, NavGeometryKind::placeable_pwk, ResourceType::pwk,
            NavGeometryNodeSelection::all);
    }
    stats.door_count = area.doors.size();
    for (auto* door : area.doors) {
        append_object(door, NavGeometryKind::door_dwk, ResourceType::dwk,
            NavGeometryNodeSelection::door_closed);
    }

    Vector<AreaObjectTransformInput> transform_inputs;
    transform_inputs.reserve(object_rows.size());
    for (const auto& row : object_rows)
        transform_inputs.push_back(row.transform);
    Vector<glm::mat4> transforms(object_rows.size(), glm::mat4{1.0f});
    const auto transform_stats = build_area_object_world_transforms(transform_inputs, transforms);
    stats.rejected_object_count += transform_stats.rejected_count;

    struct CachedModel {
        std::unique_ptr<model::Mdl> model;
        bool valid = false;
        bool empty = false;
    };
    Vector<CachedModel> cached_models;
    absl::flat_hash_map<Resource, size_t> resource_indices;
    resource_indices.reserve(object_rows.size());
    Vector<NavGeometryModelInput> append_inputs;
    append_inputs.reserve(object_rows.size());

    for (size_t i = 0; i < object_rows.size(); ++i) {
        const auto [iterator, inserted] = resource_indices.try_emplace(
            object_rows[i].resource, cached_models.size());
        if (inserted) {
            auto data = resources.demand(object_rows[i].resource);
            if (data.bytes.size() == 0) {
                cached_models.push_back({nullptr, false, true});
            } else {
                auto parsed = std::make_unique<model::Mdl>(std::move(data));
                const bool valid = parsed->valid();
                cached_models.push_back({std::move(parsed), valid, false});
            }
        }
        const auto& cached = cached_models[iterator->second];
        if (cached.empty) {
            ++stats.empty_walkmesh_count;
            continue;
        }
        if (!cached.valid) {
            ++stats.rejected_object_count;
            continue;
        }
        append_inputs.push_back({
            .model = &cached.model->model,
            .transform = transforms[i],
            .kind = object_rows[i].kind,
            .node_selection = object_rows[i].node_selection,
            .owner = object_rows[i].owner,
        });
    }

    stats.unique_resource_count = cached_models.size();
    stats.append = append_nav_geometry_models(append_inputs, output);
    return stats;
}

NavSurfaceCatalogStats build_nav_surface_walkability(
    const StaticTwoDA& surfaces,
    Vector<uint8_t>& output)
{
    NavSurfaceCatalogStats stats;
    stats.row_count = surfaces.rows();
    output.assign(stats.row_count, 0);

    for (size_t row = 0; row < stats.row_count; ++row) {
        int32_t walk = 0;
        if (!surfaces.get_to(row, "Walk", walk, false) || (walk != 0 && walk != 1)) {
            ++stats.invalid_count;
            continue;
        }
        output[row] = static_cast<uint8_t>(walk);
        stats.walkable_count += static_cast<size_t>(walk);
    }
    return stats;
}

} // namespace nw::nav
