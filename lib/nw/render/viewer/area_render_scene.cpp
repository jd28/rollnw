#include "area_render_scene.hpp"

#include "preview_scene.hpp"

#include <nw/render/model_asset.hpp>
#include <nw/render/model_renderer.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/render_service.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace nw::render::viewer {
namespace {

static constexpr uint32_t kInvalidChunkId = std::numeric_limits<uint32_t>::max();
static constexpr uint32_t kSortedVisibleStaticSurfaceThreshold = 4096;
static constexpr uint32_t kSortedVisibleStaticSurfaceMinimumSceneCoveragePercent = 50;

uint32_t saturating_count(size_t value)
{
    return static_cast<uint32_t>(std::min<size_t>(value, std::numeric_limits<uint32_t>::max()));
}

void add_saturating(uint32_t& target, uint32_t value) noexcept
{
    const uint64_t next = static_cast<uint64_t>(target) + static_cast<uint64_t>(value);
    target = static_cast<uint32_t>(std::min<uint64_t>(next, std::numeric_limits<uint32_t>::max()));
}

const AreaRenderSourceInfo& source_info_for_model(const PreviewScene& scene, size_t index)
{
    static const AreaRenderSourceInfo kDefaultInfo{};
    if (index < scene.area_model_info.size()) {
        return scene.area_model_info[index];
    }
    return kDefaultInfo;
}

const AreaRenderSourceInfo& source_info_for_render_model(const PreviewScene& scene, size_t index)
{
    static const AreaRenderSourceInfo kDefaultInfo{};
    if (index < scene.static_area_model_info.size()) {
        return scene.static_area_model_info[index];
    }
    return kDefaultInfo;
}

bool has_opaque_cutout_pass(uint8_t pass_mask) noexcept
{
    return (pass_mask & 0x1u) != 0;
}

bool has_water_pass(uint8_t pass_mask) noexcept
{
    return (pass_mask & 0x2u) != 0;
}

bool has_transparent_pass(uint8_t pass_mask) noexcept
{
    return (pass_mask & 0x4u) != 0;
}

bool has_flag(uint8_t flags, AreaRenderScene::RecordFlag flag) noexcept
{
    return (flags & static_cast<uint8_t>(flag)) != 0;
}

void set_flag(uint8_t& flags, AreaRenderScene::RecordFlag flag, bool enabled) noexcept
{
    if (enabled) {
        flags |= static_cast<uint8_t>(flag);
    } else {
        flags &= static_cast<uint8_t>(~static_cast<uint8_t>(flag));
    }
}

uint8_t render_model_pass_mask(const nw::render::RenderModel& model) noexcept
{
    uint8_t result = 0;
    for (const auto& primitive : model.primitives) {
        if (primitive.index_count == 0 || primitive.material >= model.materials.size()) {
            continue;
        }
        switch (model.materials[primitive.material].alpha_mode) {
        case nw::render::MaterialMode::opaque:
        case nw::render::MaterialMode::cutout:
            result |= 0x1u;
            break;
        case nw::render::MaterialMode::water:
            result |= 0x2u;
            break;
        case nw::render::MaterialMode::transparent:
            result |= 0x4u;
            break;
        }
    }
    return result;
}

bool render_model_casts_shadow(const nw::render::RenderModel& model) noexcept
{
    const auto summary = model.shadow.valid
        ? model.shadow
        : nw::render::summarize_render_model_shadows(model);
    return summary.casts_shadow;
}

void append_area_prepared_model_draws(
    nw::render::PreparedModelDrawList& out,
    nw::render::ModelInstanceHandle handle,
    bool valid_instance,
    bool instance_casts_shadow,
    uint32_t model_index,
    uint32_t source_draw_begin,
    uint32_t source_draw_end,
    std::span<const nwn::PreparedDrawItem> sidecar_draws)
{
    ++out.stats.handle_count;
    if (!valid_instance) {
        ++out.stats.stale_handle_count;
        out.instance_offsets.push_back(saturating_count(out.draws.size()));
        return;
    }
    if (source_draw_begin >= source_draw_end || source_draw_begin >= sidecar_draws.size()) {
        out.instance_offsets.push_back(saturating_count(out.draws.size()));
        return;
    }

    ++out.stats.nwn_legacy_instance_count;
    const uint32_t clamped_end = std::min<uint32_t>(source_draw_end, saturating_count(sidecar_draws.size()));
    out.draws.reserve(out.draws.size() + static_cast<size_t>(clamped_end - source_draw_begin));
    for (uint32_t draw_index = source_draw_begin; draw_index < clamped_end; ++draw_index) {
        const auto& sidecar_draw = sidecar_draws[draw_index];
        const auto* mesh = sidecar_draw.mesh;
        if (!mesh) {
            ++out.stats.invalid_draw_count;
            continue;
        }

        out.draws.push_back(nw::render::PreparedModelDraw{
            .instance = handle,
            .instance_kind = nw::render::ModelInstanceKind::nwn_legacy,
            .kind = nw::render::PreparedModelDrawKind::nwn_legacy,
            .instance_source_index = model_index,
            .source_draw_index = draw_index,
            .material_override = {},
            .material_mode = mesh->material_mode,
            .material_uses_fallback = mesh->material_uses_fallback,
            .material_payload = nw::render::prepared_nwn_legacy_material_payload_kind(
                mesh->uses_plt,
                mesh->material_uses_fallback),
            .skinned = mesh->is_skin,
            .instance_casts_shadow = instance_casts_shadow,
            .world = sidecar_draw.world,
            .normal_matrix = sidecar_draw.normal_matrix,
            .bounds = sidecar_draw.light_bounds,
        });
        ++out.stats.nwn_legacy_draw_count;
        if (mesh->material_uses_fallback) {
            add_saturating(out.stats.material_fallback_draw_count, 1u);
            add_saturating(out.stats.nwn_legacy_material_fallback_draw_count, 1u);
        }
    }
    out.instance_offsets.push_back(saturating_count(out.draws.size()));
}

std::span<const uint32_t> prepared_surface_index_span(
    std::span<const uint32_t> indices,
    const std::array<uint32_t, 5>& offsets,
    uint32_t first_pass_index,
    uint32_t one_past_last_pass_index) noexcept
{
    if (first_pass_index >= one_past_last_pass_index || one_past_last_pass_index >= offsets.size()) {
        return {};
    }
    const uint32_t begin = offsets[first_pass_index];
    const uint32_t end = offsets[one_past_last_pass_index];
    if (end <= begin || end > indices.size()) {
        return {};
    }
    return std::span<const uint32_t>{
        indices.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

bool prepared_surface_index_less(
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    uint32_t lhs,
    uint32_t rhs) noexcept
{
    const bool lhs_valid = lhs < surfaces.size();
    const bool rhs_valid = rhs < surfaces.size();
    if (lhs_valid != rhs_valid) {
        return lhs_valid;
    }
    if (!lhs_valid) {
        return lhs < rhs;
    }
    const auto& lhs_surface = surfaces[lhs];
    const auto& rhs_surface = surfaces[rhs];
    if (nw::render::prepared_model_surface_sort_less(lhs_surface, rhs_surface)) {
        return true;
    }
    if (nw::render::prepared_model_surface_sort_less(rhs_surface, lhs_surface)) {
        return false;
    }
    return lhs < rhs;
}

void sort_prepared_surface_indices(
    std::vector<uint32_t>& indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces)
{
    std::sort(indices.begin(), indices.end(), [&](uint32_t lhs, uint32_t rhs) noexcept {
        return prepared_surface_index_less(surfaces, lhs, rhs);
    });
}

void rebuild_prepared_surface_pass_offsets(
    std::array<uint32_t, 5>& offsets,
    std::span<const uint32_t> indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces) noexcept
{
    offsets.fill(saturating_count(indices.size()));
    uint32_t cursor = 0;
    for (uint32_t pass_index = 0; pass_index < 4u; ++pass_index) {
        offsets[pass_index] = cursor;
        while (cursor < indices.size()) {
            const uint32_t surface_index = indices[cursor];
            if (surface_index >= surfaces.size()
                || nw::render::prepared_model_surface_pass_index(surfaces[surface_index].material_mode)
                    != pass_index) {
                break;
            }
            ++cursor;
        }
    }
    offsets[4] = cursor;
}

void rebuild_prepared_surface_indices(
    std::vector<uint32_t>& indices,
    std::array<uint32_t, 5>& offsets,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces)
{
    indices.clear();
    const uint32_t count = saturating_count(surfaces.size());
    indices.reserve(count);
    for (uint32_t surface_index = 0; surface_index < count; ++surface_index) {
        indices.push_back(surface_index);
    }
    sort_prepared_surface_indices(indices, surfaces);
    rebuild_prepared_surface_pass_offsets(offsets, indices, surfaces);
}

uint32_t area_sidecar_draw_index_for_common_draw(
    const AreaRenderScene& scene,
    const nw::render::PreparedModelDraw& draw) noexcept
{
    if (draw.kind != nw::render::PreparedModelDrawKind::nwn_legacy
        || draw.instance_kind != nw::render::ModelInstanceKind::nwn_legacy) {
        return nw::render::kInvalidPreparedModelDrawIndex;
    }

    const auto sidecar_draws = scene.prepared_draws();
    if (draw.source_draw_index >= sidecar_draws.size()) {
        return nw::render::kInvalidPreparedModelDrawIndex;
    }

    const auto& sidecar_draw = sidecar_draws[draw.source_draw_index];
    const auto* mesh = sidecar_draw.mesh;
    const auto expected_payload = mesh
        ? nw::render::prepared_nwn_legacy_material_payload_kind(mesh->uses_plt, mesh->material_uses_fallback)
        : nw::render::PreparedModelMaterialPayloadKind::fallback;
    if (!mesh || mesh->material_mode != draw.material_mode
        || mesh->material_uses_fallback != draw.material_uses_fallback
        || draw.material_payload != expected_payload
        || mesh->is_skin != draw.skinned) {
        return nw::render::kInvalidPreparedModelDrawIndex;
    }
    return draw.source_draw_index;
}

bool area_surface_matches_common_draw(
    const nw::render::PreparedModelSurfaceDraw& surface,
    const nw::render::PreparedModelDraw& draw) noexcept
{
    return surface.instance == draw.instance
        && surface.instance_kind == nw::render::ModelInstanceKind::nwn_legacy
        && surface.instance_kind == draw.instance_kind
        && surface.payload_kind == nw::render::PreparedModelDrawKind::nwn_legacy
        && surface.payload_kind == draw.kind
        && surface.instance_source_index == draw.instance_source_index
        && surface.source_draw_index == draw.source_draw_index
        && surface.material_index == draw.material_index
        && surface.material_override == draw.material_override
        && surface.skin_index == draw.skin_index
        && surface.material_mode == draw.material_mode
        && surface.material_uses_fallback == draw.material_uses_fallback
        && surface.material_payload == draw.material_payload
        && surface.skinned == draw.skinned
        && surface.world == draw.world
        && surface.normal_matrix == draw.normal_matrix;
}

uint32_t area_sidecar_draw_index_for_surface(
    const AreaRenderScene& scene,
    const nw::render::PreparedModelSurfaceDraw& surface) noexcept
{
    if (surface.draw_index >= scene.prepared_model_draw_list().draws.size()) {
        return nw::render::kInvalidPreparedModelDrawIndex;
    }
    const auto& draw = scene.prepared_model_draw_list().draws[surface.draw_index];
    if (!area_surface_matches_common_draw(surface, draw)) {
        return nw::render::kInvalidPreparedModelDrawIndex;
    }
    return area_sidecar_draw_index_for_common_draw(scene, draw);
}

const nwn::PreparedDrawItem* area_sidecar_draw_for_surface(
    const AreaRenderScene& scene,
    const nw::render::PreparedModelSurfaceDraw& surface) noexcept
{
    const uint32_t draw_index = area_sidecar_draw_index_for_surface(scene, surface);
    const auto sidecar_draws = scene.prepared_draws();
    return draw_index < sidecar_draws.size() ? &sidecar_draws[draw_index] : nullptr;
}

bool area_common_records_include_sidecar_draw(
    const AreaRenderScene& scene,
    uint32_t record_index,
    uint32_t source_draw_index) noexcept
{
    for (const auto& surface : scene.prepared_model_surface_draws_for_record(record_index)) {
        if (surface.source_draw_index == source_draw_index
            && area_sidecar_draw_for_surface(scene, surface)) {
            return true;
        }
    }
    return false;
}

bool record_uses_static_prepared_legacy_path(
    const AreaRenderScene& scene,
    uint32_t record_index) noexcept
{
    const auto flags = scene.flags();
    return record_index < flags.size()
        && has_flag(flags[record_index], AreaRenderScene::RecordFlag::static_candidate)
        && !scene.prepared_draws_for_record(record_index).empty();
}

void append_visible_legacy_static_prepared_surface_indices(
    std::vector<uint32_t>& out,
    const AreaRenderScene& scene,
    std::span<const uint32_t> source_surface_indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> prepared_surfaces,
    std::span<const uint32_t> visible_record_marks,
    uint32_t visible_record_generation)
{
    const auto sidecar_draws = scene.prepared_draws();
    if (sidecar_draws.empty()) {
        return;
    }

    for (const uint32_t surface_index : source_surface_indices) {
        if (surface_index >= prepared_surfaces.size()) {
            continue;
        }
        const auto& surface = prepared_surfaces[surface_index];
        const uint32_t draw_index = area_sidecar_draw_index_for_surface(scene, surface);
        if (draw_index >= sidecar_draws.size()
            || !area_common_records_include_sidecar_draw(scene, surface.handle_index, draw_index)) {
            continue;
        }
        const uint32_t record_index = surface.handle_index;
        if (record_index < visible_record_marks.size()
            && visible_record_marks[record_index] == visible_record_generation) {
            out.push_back(surface_index);
        }
    }
}

void append_area_visible_render_model_handle(
    std::vector<nw::render::ModelInstanceHandle>& out,
    const AreaRenderScene& scene,
    uint32_t record_index)
{
    const auto kinds = scene.model_kinds();
    const auto handles = scene.model_instance_handles();
    if (record_index >= kinds.size()
        || record_index >= handles.size()
        || kinds[record_index] != nw::render::ModelInstanceKind::render_model) {
        return;
    }

    const auto handle = handles[record_index];
    if (handle.valid()) {
        out.push_back(handle);
    }
}

void append_area_visible_nwn_legacy_handle(
    std::vector<nw::render::ModelInstanceHandle>& out,
    const AreaRenderScene& scene,
    uint32_t record_index)
{
    const auto kinds = scene.model_kinds();
    const auto handles = scene.model_instance_handles();
    const auto flags = scene.flags();
    if (record_index >= kinds.size()
        || record_index >= handles.size()
        || record_index >= flags.size()
        || kinds[record_index] != nw::render::ModelInstanceKind::nwn_legacy
        || has_flag(flags[record_index], AreaRenderScene::RecordFlag::static_candidate)) {
        return;
    }

    const auto handle = handles[record_index];
    if (handle.valid()) {
        out.push_back(handle);
    }
}

void append_area_direct_model_record(
    AreaDirectModelRecordSelection& out,
    const AreaRenderScene& scene,
    uint32_t record_index,
    bool include_render_model_records)
{
    const auto kinds = scene.model_kinds();
    const auto model_indices = scene.model_indices();
    if (record_index >= kinds.size()
        || record_index >= model_indices.size()) {
        return;
    }

    const uint32_t model_index = model_indices[record_index];
    switch (kinds[record_index]) {
    case nw::render::ModelInstanceKind::nwn_legacy: {
        const auto models = scene.models();
        if (record_index >= models.size()
            || !models[record_index]
            || scene.record_index_for_model(model_index) != record_index) {
            return;
        }
        if (!record_uses_static_prepared_legacy_path(scene, record_index)) {
            out.records.push_back(AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::nwn_legacy,
                .record_index = record_index,
            });
        }
        break;
    }
    case nw::render::ModelInstanceKind::render_model:
        if (include_render_model_records
            && scene.record_index_for_render_model(model_index) == record_index) {
            out.records.push_back(AreaDirectModelRecord{
                .kind = nw::render::ModelInstanceKind::render_model,
                .record_index = record_index,
            });
        }
        break;
    }
}
glm::vec4 matrix_row(const glm::mat4& matrix, glm::length_t row) noexcept
{
    return glm::vec4(matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
}

struct AreaRenderFrustum {
    std::array<glm::vec4, 6> planes{};
    bool valid = false;
};

AreaRenderFrustum make_area_render_frustum(const glm::mat4& view_projection) noexcept
{
    const glm::vec4 row0 = matrix_row(view_projection, 0);
    const glm::vec4 row1 = matrix_row(view_projection, 1);
    const glm::vec4 row2 = matrix_row(view_projection, 2);
    const glm::vec4 row3 = matrix_row(view_projection, 3);

    AreaRenderFrustum result{};
    result.planes = {{
        row3 + row0,
        row3 - row0,
        row3 + row1,
        row3 - row1,
        row2,
        row3 - row2,
    }};

    result.valid = true;
    for (auto& plane : result.planes) {
        const float length = glm::length(glm::vec3{plane});
        if (length <= 1.0e-6f) {
            result.valid = false;
            continue;
        }
        plane /= length;
    }
    return result;
}

bool bounds_intersects_frustum(const Bounds& bounds, const AreaRenderFrustum& frustum) noexcept
{
    if (!frustum.valid) {
        return true;
    }

    constexpr float kCullPadding = 0.25f;
    for (const glm::vec4& plane : frustum.planes) {
        const glm::vec3 normal{plane};
        const glm::vec3 positive{
            normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
            normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
            normal.z >= 0.0f ? bounds.max.z : bounds.min.z,
        };
        if (glm::dot(normal, positive) + plane.w < -kCullPadding) {
            return false;
        }
    }
    return true;
}

void expand_bounds(Bounds& target, const Bounds& source, bool& initialized) noexcept
{
    if (!initialized) {
        target = source;
        initialized = true;
        return;
    }
    target.min = glm::min(target.min, source.min);
    target.max = glm::max(target.max, source.max);
}

void count_kind(AreaRenderSceneStats& stats, AreaRenderRecordKind kind) noexcept
{
    switch (kind) {
    case AreaRenderRecordKind::tile:
        ++stats.tile_record_count;
        break;
    case AreaRenderRecordKind::creature:
        ++stats.creature_record_count;
        break;
    case AreaRenderRecordKind::door:
        ++stats.door_record_count;
        break;
    case AreaRenderRecordKind::item:
        ++stats.item_record_count;
        break;
    case AreaRenderRecordKind::placeable:
        ++stats.placeable_record_count;
        break;
    case AreaRenderRecordKind::unknown:
        ++stats.unknown_record_count;
        break;
    }
}

uint32_t area_chunk_id(const PreviewScene& scene, const AreaRenderSourceInfo& info, const nw::render::Bounds& bounds)
{
    if (scene.area_width <= 0 || scene.area_height <= 0) {
        return kInvalidChunkId;
    }

    int32_t chunk_x = info.tile_x;
    int32_t chunk_y = info.tile_y;
    if (chunk_x < 0 || chunk_y < 0) {
        const glm::vec3 center = bounds.center();
        chunk_x = static_cast<int32_t>(std::floor(center.x / kAreaRenderTileSize));
        chunk_y = static_cast<int32_t>(std::floor(center.y / kAreaRenderTileSize));
    }

    chunk_x = std::clamp(chunk_x, 0, scene.area_width - 1);
    chunk_y = std::clamp(chunk_y, 0, scene.area_height - 1);
    return static_cast<uint32_t>(chunk_y * scene.area_width + chunk_x);
}

bool static_batched_geometry_less(const nwn::PreparedDrawItem* lhs, const nwn::PreparedDrawItem* rhs) noexcept
{
    if (lhs == rhs) {
        return false;
    }
    if (!lhs || !lhs->mesh) {
        return rhs && rhs->mesh;
    }
    if (!rhs || !rhs->mesh) {
        return false;
    }
    if (lhs->mesh->vertices != rhs->mesh->vertices) {
        return lhs->mesh->vertices < rhs->mesh->vertices;
    }
    if (lhs->mesh->indices != rhs->mesh->indices) {
        return lhs->mesh->indices < rhs->mesh->indices;
    }
    if (lhs->mesh->index_count != rhs->mesh->index_count) {
        return lhs->mesh->index_count < rhs->mesh->index_count;
    }
    return lhs->order < rhs->order;
}

struct StaticPreparedSurfaceIndex {
    uint32_t surface_index = nw::render::kInvalidPreparedModelDrawIndex;
    uint32_t draw_index = nw::render::kInvalidPreparedModelDrawIndex;
};

struct StaticAreaGeometryEntry {
    nw::gfx::Handle<nw::gfx::Buffer> source_vertices;
    nw::gfx::Handle<nw::gfx::Buffer> source_indices;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
};

bool static_area_geometry_matches(const StaticAreaGeometryEntry& entry, const nwn::Mesh& mesh) noexcept
{
    return entry.source_vertices == mesh.vertices
        && entry.source_indices == mesh.indices
        && entry.vertex_count == mesh.vertex_count
        && entry.index_count == mesh.index_count;
}

bool supports_batched_shadow_draw(const nwn::PreparedDrawItem& draw) noexcept
{
    return draw.mesh
        && !draw.mesh->is_skin
        && draw.mesh->material_mode != nw::render::MaterialMode::transparent
        && draw.mesh->material_mode != nw::render::MaterialMode::water;
}

bool supports_static_material_draw_data_cache(const nwn::PreparedDrawItem& draw) noexcept
{
    return draw.mesh
        && !draw.mesh->is_skin
        && draw.static_area_geometry.valid()
        && (draw.mesh->material_mode == nw::render::MaterialMode::opaque
            || draw.mesh->material_mode == nw::render::MaterialMode::cutout);
}

StaticAreaGeometryEntry* find_static_area_geometry_entry(
    std::vector<StaticAreaGeometryEntry>& entries, const nwn::Mesh& mesh) noexcept
{
    const auto it = std::find_if(entries.begin(), entries.end(),
        [&](const StaticAreaGeometryEntry& entry) {
            return static_area_geometry_matches(entry, mesh);
        });
    return it == entries.end() ? nullptr : &*it;
}

uint64_t index_list_signature(std::span<const uint32_t> indices) noexcept
{
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&](uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix(indices.size());
    for (const uint32_t index : indices) {
        mix(index);
    }
    return hash;
}

uint64_t index_cache_signature(
    std::span<const uint32_t> indices,
    size_t source_count) noexcept
{
    uint64_t hash = index_list_signature(indices);
    hash ^= source_count;
    hash *= 1099511628211ull;
    return hash;
}

AreaPreparedSurfaceSidecarStats append_prepared_surface_draw_pointers(
    std::vector<const nwn::PreparedDrawItem*>& out,
    const AreaRenderScene& scene,
    std::span<const uint32_t> surface_indices)
{
    AreaPreparedSurfaceSidecarStats stats{};
    stats.input_surface_count = saturating_count(surface_indices.size());
    out.reserve(out.size() + surface_indices.size());

    const auto& surfaces = scene.prepared_model_surface_draws().draws;
    const auto& common_draws = scene.prepared_model_draw_list().draws;
    const auto sidecar_draws = scene.prepared_draws();
    for (const uint32_t surface_index : surface_indices) {
        if (surface_index >= surfaces.size()) {
            add_saturating(stats.dropped_invalid_surface_index_count, 1u);
            continue;
        }
        const auto& surface = surfaces[surface_index];
        if (surface.payload_kind != nw::render::PreparedModelDrawKind::nwn_legacy
            || surface.instance_kind != nw::render::ModelInstanceKind::nwn_legacy) {
            add_saturating(stats.dropped_non_nwn_surface_count, 1u);
            continue;
        }
        if (surface.draw_index >= common_draws.size() || surface.source_draw_index >= sidecar_draws.size()) {
            add_saturating(stats.dropped_missing_sidecar_draw_count, 1u);
            continue;
        }
        const auto& draw = common_draws[surface.draw_index];
        if (!area_surface_matches_common_draw(surface, draw)) {
            add_saturating(stats.dropped_invalid_sidecar_draw_count, 1u);
            continue;
        }
        const uint32_t draw_index = area_sidecar_draw_index_for_common_draw(scene, draw);
        if (draw_index >= sidecar_draws.size()) {
            add_saturating(stats.dropped_invalid_sidecar_draw_count, 1u);
            continue;
        }
        out.push_back(&sidecar_draws[draw_index]);
        add_saturating(stats.selected_draw_count, 1u);
    }
    return stats;
}

AreaPreparedSurfaceSidecarStats append_prepared_surface_draw_pointers(
    std::vector<const nwn::PreparedDrawItem*>& out,
    std::span<const uint32_t> surface_indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const nwn::PreparedDrawItem> sidecar_draws)
{
    AreaPreparedSurfaceSidecarStats stats{};
    stats.input_surface_count = saturating_count(surface_indices.size());
    out.reserve(out.size() + surface_indices.size());

    for (const uint32_t surface_index : surface_indices) {
        if (surface_index >= surfaces.size()) {
            add_saturating(stats.dropped_invalid_surface_index_count, 1u);
            continue;
        }
        const auto& surface = surfaces[surface_index];
        if (surface.payload_kind != nw::render::PreparedModelDrawKind::nwn_legacy
            || surface.instance_kind != nw::render::ModelInstanceKind::nwn_legacy) {
            add_saturating(stats.dropped_non_nwn_surface_count, 1u);
            continue;
        }
        if (surface.source_draw_index >= sidecar_draws.size()) {
            add_saturating(stats.dropped_missing_sidecar_draw_count, 1u);
            continue;
        }
        out.push_back(&sidecar_draws[surface.source_draw_index]);
        add_saturating(stats.selected_draw_count, 1u);
    }
    return stats;
}

std::span<const nwn::PreparedDrawItem* const> prepared_draw_pointer_bridge_span_for_surfaces(
    nw::render::nwn::PreparedDrawScratch& scratch,
    const AreaRenderScene& scene,
    std::span<const uint32_t> surface_indices,
    AreaPreparedSurfaceSidecarStats* stats = nullptr)
{
    // Common-surface bridge materialization for the current NWN renderer entry
    // points. Out-of-range, stale, or non-NWN surfaces are dropped and counted
    // at the boundary instead of leaking undefined sidecar state into submission.
    auto& out = scratch.bridge_draws;
    out.clear();
    const auto bridge_stats = append_prepared_surface_draw_pointers(out, scene, surface_indices);
    if (stats) {
        stats->add(bridge_stats);
    }
    return out;
}

using PreparedIndirectBuildFn = bool (*)(
    std::vector<nw::gfx::IndexedIndirectDrawCommand>&,
    nwn::PreparedIndirectDrawCommands&,
    std::span<const nwn::PreparedDrawItem* const>,
    nw::render::MaterialMode);

void refresh_prepared_indirect_cache_from_pointers(
    AreaPreparedIndirectDrawCache& cache,
    std::span<const nwn::PreparedDrawItem* const> draw_pointers,
    uint64_t signature,
    uint32_t source_index_count,
    nw::render::MaterialMode mode,
    PreparedIndirectBuildFn build)
{
    if (draw_pointers.empty()) {
        cache.clear();
        return;
    }

    if (cache.signature == signature && cache.source_index_count == source_index_count) {
        return;
    }

    cache.signature = signature;
    cache.source_index_count = source_index_count;
    cache.clear_gpu_commands();
    if (!build(cache.commands, cache.indirect, draw_pointers, mode)) {
        cache.indirect = {};
        cache.clear_gpu_commands();
    }
}

void refresh_prepared_indirect_cache_for_surfaces(
    AreaPreparedIndirectDrawCache& cache,
    const AreaRenderScene& scene,
    std::span<const uint32_t> surface_indices,
    nw::render::MaterialMode mode,
    AreaPreparedSurfaceSidecarStats* stats = nullptr)
{
    if (surface_indices.empty() || surface_indices.size() > std::numeric_limits<uint32_t>::max()) {
        cache.clear();
        return;
    }

    std::vector<const nwn::PreparedDrawItem*> draw_pointers;
    const auto bridge_stats = append_prepared_surface_draw_pointers(draw_pointers, scene, surface_indices);
    if (stats) {
        stats->add(bridge_stats);
    }
    refresh_prepared_indirect_cache_from_pointers(
        cache,
        draw_pointers,
        index_cache_signature(surface_indices, scene.prepared_model_surface_draws().draws.size()),
        static_cast<uint32_t>(surface_indices.size()),
        mode,
        nwn::build_prepared_indirect_draw_commands);
}

void refresh_prepared_static_material_indirect_cache_for_surfaces(
    AreaPreparedIndirectDrawCache& cache,
    const AreaRenderScene& scene,
    std::span<const uint32_t> surface_indices,
    nw::render::MaterialMode mode,
    AreaPreparedSurfaceSidecarStats* stats = nullptr)
{
    if (surface_indices.empty() || surface_indices.size() > std::numeric_limits<uint32_t>::max()) {
        cache.clear();
        return;
    }

    std::vector<const nwn::PreparedDrawItem*> draw_pointers;
    const auto bridge_stats = append_prepared_surface_draw_pointers(draw_pointers, scene, surface_indices);
    if (stats) {
        stats->add(bridge_stats);
    }
    refresh_prepared_indirect_cache_from_pointers(
        cache,
        draw_pointers,
        index_cache_signature(surface_indices, scene.prepared_model_surface_draws().draws.size()),
        static_cast<uint32_t>(surface_indices.size()),
        mode,
        nwn::build_prepared_static_material_indirect_draw_commands);
}

} // namespace

AreaPreparedSurfaceSidecarStats collect_area_prepared_surface_sidecar_draws(
    std::vector<const nwn::PreparedDrawItem*>& out,
    const AreaRenderScene& scene,
    std::span<const uint32_t> surface_indices)
{
    out.clear();
    return append_prepared_surface_draw_pointers(out, scene, surface_indices);
}

AreaPreparedSurfaceSidecarStats collect_area_prepared_surface_sidecar_draws(
    std::vector<const nwn::PreparedDrawItem*>& out,
    std::span<const uint32_t> surface_indices,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const nwn::PreparedDrawItem> sidecar_draws)
{
    out.clear();
    return append_prepared_surface_draw_pointers(out, surface_indices, surfaces, sidecar_draws);
}

bool should_use_sorted_area_static_surface_lists(
    uint32_t visible_prepared_surface_count,
    uint32_t total_prepared_surface_count) noexcept
{
    if (visible_prepared_surface_count < kSortedVisibleStaticSurfaceThreshold || total_prepared_surface_count == 0u) {
        return false;
    }
    if (visible_prepared_surface_count >= total_prepared_surface_count) {
        return true;
    }
    return static_cast<uint64_t>(visible_prepared_surface_count) * 100u
        >= static_cast<uint64_t>(total_prepared_surface_count)
        * kSortedVisibleStaticSurfaceMinimumSceneCoveragePercent;
}

AreaPreparedSurfaceSidecarStats refresh_prepared_shadow_indirect_cache(
    AreaShadowCascadeDraws& cascade_draws,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const nwn::PreparedDrawItem> draws)
{
    auto& cache = cascade_draws.prepared_indirect;
    const auto surface_indices = std::span<const uint32_t>{cascade_draws.prepared_surface_indices};
    if (surface_indices.empty() || surface_indices.size() > std::numeric_limits<uint32_t>::max()) {
        cache.clear();
        return {};
    }

    const uint64_t signature = cascade_draws.finalized_prepared_surface_signature(surfaces.size());
    const auto source_index_count = static_cast<uint32_t>(surface_indices.size());
    if (cache.signature == signature && cache.source_index_count == source_index_count) {
        return {};
    }

    std::vector<const nwn::PreparedDrawItem*> draw_pointers;
    const auto stats = append_prepared_surface_draw_pointers(draw_pointers, surface_indices, surfaces, draws);
    if (draw_pointers.empty()) {
        cache.clear();
        return stats;
    }

    cache.signature = signature;
    cache.source_index_count = source_index_count;
    cache.clear_gpu_commands();
    if (!nwn::build_prepared_shadow_indirect_draw_commands(cache.commands, cache.indirect, draw_pointers)) {
        cache.indirect = {};
        cache.clear_gpu_commands();
    }
    return stats;
}

AreaStaticMaterialDrawBatches area_static_material_draw_batches_for_pass(
    const AreaRenderFrame& frame,
    nw::render::RenderPassSelection pass,
    nw::gfx::StorageSpan static_material_draw_data) noexcept
{
    AreaStaticMaterialDrawBatches result{};
    const bool sorted_static_order = frame.uses_sorted_static_draw_lists();
    const auto append = [&](std::span<const uint32_t> surface_indices,
                            nw::render::MaterialMode material,
                            bool static_geometry_sorted,
                            nwn::PreparedIndirectDrawCommands indirect_draws = {},
                            nw::gfx::StorageSpan cached_draw_data = {}) {
        if (result.count >= result.batches.size()) {
            return;
        }
        result.batches[result.count++] = AreaStaticMaterialDrawBatch{
            .surface_indices = surface_indices,
            .material = material,
            .static_geometry_sorted = static_geometry_sorted,
            .indirect_draws = indirect_draws,
            .cached_draw_data = cached_draw_data,
        };
    };

    switch (pass) {
    case nw::render::RenderPassSelection::opaque_cutout:
        append(
            sorted_static_order
                ? frame.visible_opaque_static_prepared_surface_indices()
                : frame.visible_opaque_prepared_surface_indices(),
            nw::render::MaterialMode::opaque,
            sorted_static_order,
            static_material_draw_data.buffer.valid()
                ? frame.visible_opaque_static_material_indirect_draws()
                : frame.visible_opaque_prepared_indirect_draws(),
            static_material_draw_data);
        append(
            sorted_static_order
                ? frame.visible_cutout_static_prepared_surface_indices()
                : frame.visible_cutout_prepared_surface_indices(),
            nw::render::MaterialMode::cutout,
            sorted_static_order,
            static_material_draw_data.buffer.valid()
                ? frame.visible_cutout_static_material_indirect_draws()
                : frame.visible_cutout_prepared_indirect_draws(),
            static_material_draw_data);
        break;
    case nw::render::RenderPassSelection::water:
        append(
            frame.visible_water_prepared_surface_indices(),
            nw::render::MaterialMode::water,
            false);
        break;
    case nw::render::RenderPassSelection::transparent:
        append(
            frame.visible_transparent_prepared_surface_indices(),
            nw::render::MaterialMode::transparent,
            false);
        break;
    case nw::render::RenderPassSelection::all:
        break;
    }
    return result;
}

AreaStaticMaterialDrawBatch area_static_material_depth_prepass_batch(
    const AreaRenderFrame& frame,
    nw::gfx::StorageSpan static_material_draw_data) noexcept
{
    const bool sorted_static_order = frame.uses_sorted_static_draw_lists();
    return AreaStaticMaterialDrawBatch{
        .surface_indices = sorted_static_order
            ? frame.visible_opaque_static_prepared_surface_indices()
            : frame.visible_opaque_prepared_surface_indices(),
        .material = nw::render::MaterialMode::opaque,
        .static_geometry_sorted = sorted_static_order,
        .indirect_draws = static_material_draw_data.buffer.valid()
            ? frame.visible_opaque_static_material_indirect_draws()
            : frame.visible_opaque_prepared_indirect_draws(),
        .cached_draw_data = static_material_draw_data,
    };
}

namespace {

AreaPreparedSurfaceSidecarStats render_area_static_material_draw_batches_with_lazy_context(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& scene,
    std::span<const AreaStaticMaterialDrawBatch> batches,
    const nw::render::RenderContext& ctx,
    nw::render::nwn::PreparedDrawScratch& scratch)
{
    AreaPreparedSurfaceSidecarStats stats{};
    std::optional<nw::render::nwn::ModelRenderContext> nwn_render_ctx{};
    const auto legacy_context = [&]() -> nw::render::nwn::ModelRenderContext& {
        if (!nwn_render_ctx) {
            nwn_render_ctx = render_service.nwn_model_render_context();
        }
        return *nwn_render_ctx;
    };

    for (const AreaStaticMaterialDrawBatch& batch : batches) {
        if (batch.surface_indices.empty()) {
            continue;
        }
        const auto draws = prepared_draw_pointer_bridge_span_for_surfaces(
            scratch,
            scene,
            batch.surface_indices,
            &stats);
        if (draws.empty()) {
            continue;
        }
        nw::render::nwn::render_prepared_material_draws(
            legacy_context(),
            cmd,
            draws,
            batch.material,
            ctx,
            scratch,
            batch.static_geometry_sorted,
            batch.indirect_draws,
            batch.cached_draw_data);
    }
    return stats;
}

bool render_area_static_material_depth_prepass_with_lazy_context(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& scene,
    const AreaStaticMaterialDrawBatch& batch,
    const nw::render::RenderContext& ctx,
    nw::render::nwn::PreparedDrawScratch& scratch,
    AreaPreparedSurfaceSidecarStats* stats)
{
    const auto draws = prepared_draw_pointer_bridge_span_for_surfaces(
        scratch,
        scene,
        batch.surface_indices,
        stats);
    if (draws.empty()) {
        return false;
    }
    auto nwn_render_ctx = render_service.nwn_model_render_context();
    return nw::render::nwn::render_prepared_material_depth_prepass(
        nwn_render_ctx,
        cmd,
        draws,
        batch.material,
        ctx,
        scratch,
        batch.static_geometry_sorted,
        batch.indirect_draws,
        batch.cached_draw_data);
}

} // namespace

nw::gfx::StorageSpan refresh_area_static_material_draw_data(
    nw::render::RenderService& render_service,
    AreaRenderScene& scene,
    const AreaRenderFrame& frame,
    nw::render::nwn::PreparedDrawScratch& scratch,
    AreaPreparedSurfaceSidecarStats* sidecar_stats)
{
    if (!frame.uses_sorted_static_draw_lists() || scene.static_material_prepared_surface_indices().empty()) {
        return {};
    }

    const auto draws = prepared_draw_pointer_bridge_span_for_surfaces(
        scratch,
        scene,
        scene.static_material_prepared_surface_indices(),
        sidecar_stats);
    if (draws.empty()) {
        return {};
    }
    auto nwn_render_ctx = render_service.nwn_model_render_context();
    if (!nw::render::nwn::refresh_prepared_static_draw_data(
            nwn_render_ctx,
            scene.static_material_draw_data_cache(),
            draws,
            scene.static_material_prepared_surface_signature())) {
        return {};
    }
    return scene.static_material_draw_data_span();
}

AreaPreparedSurfaceSidecarStats render_area_static_material_draw_batches(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& scene,
    std::span<const AreaStaticMaterialDrawBatch> batches,
    const nw::render::RenderContext& ctx,
    nw::render::nwn::PreparedDrawScratch& scratch)
{
    if (!cmd) {
        return {};
    }

    const auto has_draws = std::any_of(
        batches.begin(),
        batches.end(),
        [](const AreaStaticMaterialDrawBatch& batch) {
            return !batch.surface_indices.empty();
        });
    if (!has_draws) {
        return {};
    }

    return render_area_static_material_draw_batches_with_lazy_context(
        render_service,
        cmd,
        scene,
        batches,
        ctx,
        scratch);
}

bool render_area_static_material_depth_prepass(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& scene,
    const AreaStaticMaterialDrawBatch& batch,
    const nw::render::RenderContext& ctx,
    nw::render::nwn::PreparedDrawScratch& scratch,
    AreaPreparedSurfaceSidecarStats* sidecar_stats)
{
    if (!cmd || batch.surface_indices.empty()) {
        return false;
    }

    return render_area_static_material_depth_prepass_with_lazy_context(
        render_service,
        cmd,
        scene,
        batch,
        ctx,
        scratch,
        sidecar_stats);
}

AreaRenderScene::~AreaRenderScene()
{
    clear();
}

void AreaRenderScene::clear()
{
    if (static_geometry_vertices_.valid()) {
        nw::gfx::destroy_buffer(static_geometry_vertices_);
        static_geometry_vertices_ = {};
    }
    if (static_geometry_indices_.valid()) {
        nw::gfx::destroy_buffer(static_geometry_indices_);
        static_geometry_indices_ = {};
    }

    model_indices_.clear();
    model_record_indices_.clear();
    render_model_record_indices_.clear();
    model_kinds_.clear();
    models_.clear();
    model_instance_handles_.clear();
    bounds_.clear();
    root_transforms_.clear();
    pass_masks_.clear();
    flags_.clear();
    chunk_ids_.clear();
    kinds_.clear();
    tile_xs_.clear();
    tile_ys_.clear();
    chunk_bounds_.clear();
    chunk_has_bounds_.clear();
    chunk_offsets_.clear();
    chunk_record_indices_.clear();
    prepared_draw_offsets_.clear();
    prepared_draws_.clear();
    prepared_model_draws_.clear();
    prepared_model_draw_ranges_.clear();
    prepared_model_surface_draws_.clear();
    prepared_surface_offsets_.clear();
    prepared_surface_indices_.clear();
    prepared_surface_pass_offsets_.fill(0u);
    opaque_static_prepared_surface_indices_.clear();
    cutout_static_prepared_surface_indices_.clear();
    prepared_draw_record_indices_.clear();
    shadow_prepared_surface_offsets_.clear();
    shadow_prepared_surface_indices_.clear();
    sorted_shadow_prepared_surface_indices_.clear();
    static_material_prepared_surface_indices_.clear();
    light_index_offsets_.clear();
    light_indices_.clear();
    dynamic_light_indices_.clear();
    chunk_light_index_offsets_.clear();
    chunk_light_indices_.clear();
    scene_bounds_ = {};
    stats_ = {};
    static_material_draw_data_.clear();
    static_material_prepared_surface_signature_ = 0;
    has_scene_bounds_ = false;
}

bool AreaRenderScene::build_static_geometry(nw::gfx::Context* gfx)
{
    if (!gfx || prepared_draws_.empty()) {
        return false;
    }

    std::vector<StaticAreaGeometryEntry> entries;
    entries.reserve(prepared_draws_.size());
    uint64_t total_vertex_count = 0;
    uint64_t total_index_count = 0;
    for (const auto& draw : prepared_draws_) {
        const auto* mesh = draw.mesh;
        if (!mesh || mesh->is_skin || !mesh->vertices.valid() || !mesh->indices.valid()
            || mesh->vertex_count == 0 || mesh->index_count == 0) {
            continue;
        }
        if (find_static_area_geometry_entry(entries, *mesh)) {
            continue;
        }

        if (total_vertex_count + mesh->vertex_count
                > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
            || total_index_count + mesh->index_count
                > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            return false;
        }

        entries.push_back(StaticAreaGeometryEntry{
            .source_vertices = mesh->vertices,
            .source_indices = mesh->indices,
            .vertex_count = mesh->vertex_count,
            .index_count = mesh->index_count,
            .first_index = static_cast<uint32_t>(total_index_count),
            .vertex_offset = static_cast<int32_t>(total_vertex_count),
        });
        total_vertex_count += mesh->vertex_count;
        total_index_count += mesh->index_count;
    }

    if (entries.empty() || total_vertex_count == 0 || total_index_count == 0) {
        return false;
    }

    const uint64_t vertex_bytes = total_vertex_count * sizeof(nwn::Vertex);
    const uint64_t index_bytes = total_index_count * sizeof(uint16_t);
    if (vertex_bytes > std::numeric_limits<size_t>::max()
        || index_bytes > std::numeric_limits<size_t>::max()) {
        return false;
    }

    nw::gfx::BufferDesc vertex_desc{};
    vertex_desc.size = static_cast<size_t>(vertex_bytes);
    vertex_desc.usage = nw::gfx::BufferUsage::Vertex;
    vertex_desc.cpu_visible = true;
    static_geometry_vertices_ = nw::gfx::create_buffer(gfx, vertex_desc);

    nw::gfx::BufferDesc index_desc{};
    index_desc.size = static_cast<size_t>(index_bytes);
    index_desc.usage = nw::gfx::BufferUsage::Index;
    index_desc.cpu_visible = true;
    static_geometry_indices_ = nw::gfx::create_buffer(gfx, index_desc);

    auto* vertex_dst = static_cast<uint8_t*>(nw::gfx::map_buffer(static_geometry_vertices_));
    auto* index_dst = static_cast<uint8_t*>(nw::gfx::map_buffer(static_geometry_indices_));
    if (!static_geometry_vertices_.valid() || !static_geometry_indices_.valid()
        || !vertex_dst || !index_dst) {
        if (static_geometry_vertices_.valid()) {
            nw::gfx::destroy_buffer(static_geometry_vertices_);
            static_geometry_vertices_ = {};
        }
        if (static_geometry_indices_.valid()) {
            nw::gfx::destroy_buffer(static_geometry_indices_);
            static_geometry_indices_ = {};
        }
        return false;
    }

    bool copied = true;
    for (const auto& entry : entries) {
        const auto* vertex_src = static_cast<const uint8_t*>(nw::gfx::map_buffer(entry.source_vertices));
        const auto* index_src = static_cast<const uint8_t*>(nw::gfx::map_buffer(entry.source_indices));
        if (!vertex_src || !index_src) {
            copied = false;
            break;
        }

        std::memcpy(
            vertex_dst + static_cast<size_t>(entry.vertex_offset) * sizeof(nwn::Vertex),
            vertex_src,
            static_cast<size_t>(entry.vertex_count) * sizeof(nwn::Vertex));
        std::memcpy(
            index_dst + static_cast<size_t>(entry.first_index) * sizeof(uint16_t),
            index_src,
            static_cast<size_t>(entry.index_count) * sizeof(uint16_t));
    }

    nw::gfx::unmap_buffer(static_geometry_vertices_);
    nw::gfx::unmap_buffer(static_geometry_indices_);
    if (!copied) {
        nw::gfx::destroy_buffer(static_geometry_vertices_);
        nw::gfx::destroy_buffer(static_geometry_indices_);
        static_geometry_vertices_ = {};
        static_geometry_indices_ = {};
        return false;
    }

    for (auto& draw : prepared_draws_) {
        if (!draw.mesh || draw.mesh->is_skin) {
            continue;
        }
        const auto* entry = find_static_area_geometry_entry(entries, *draw.mesh);
        if (!entry) {
            continue;
        }
        draw.static_area_geometry = nwn::PreparedDrawGeometry{
            .vertices = static_geometry_vertices_,
            .indices = static_geometry_indices_,
            .index_count = entry->index_count,
            .first_index = entry->first_index,
            .vertex_offset = entry->vertex_offset,
        };
    }

    stats_.static_geometry_mesh_count = saturating_count(entries.size());
    stats_.static_geometry_vertex_count = saturating_count(total_vertex_count);
    stats_.static_geometry_index_count = saturating_count(total_index_count);
    stats_.static_geometry_bytes = saturating_count(vertex_bytes + index_bytes);
    return true;
}

void AreaRenderScene::rebuild(const PreviewScene& scene, nw::gfx::Context* gfx)
{
    clear();
    stats_.chunk_width = scene.area_width > 0 ? static_cast<uint32_t>(scene.area_width) : 0u;
    stats_.chunk_height = scene.area_height > 0 ? static_cast<uint32_t>(scene.area_height) : 0u;
    stats_.chunk_count = stats_.chunk_width * stats_.chunk_height;
    stats_.local_light_count = saturating_count(scene.render_local_lights.size());

    const size_t record_capacity = scene.models.size() + scene.static_models.size();
    model_indices_.reserve(record_capacity);
    model_record_indices_.assign(scene.models.size(), kInvalidAreaRenderRecordIndex);
    render_model_record_indices_.assign(scene.static_models.size(), kInvalidAreaRenderRecordIndex);
    model_kinds_.reserve(record_capacity);
    models_.reserve(record_capacity);
    model_instance_handles_.reserve(record_capacity);
    bounds_.reserve(record_capacity);
    root_transforms_.reserve(record_capacity);
    pass_masks_.reserve(record_capacity);
    flags_.reserve(record_capacity);
    chunk_ids_.reserve(record_capacity);
    kinds_.reserve(record_capacity);
    tile_xs_.reserve(record_capacity);
    tile_ys_.reserve(record_capacity);
    prepared_draw_offsets_.reserve(record_capacity + 1u);
    prepared_draw_offsets_.push_back(0);
    prepared_model_draws_.instance_offsets.reserve(record_capacity + 1u);
    prepared_model_draws_.instance_offsets.push_back(0);
    std::vector<uint32_t> chunk_counts(stats_.chunk_count, 0u);
    chunk_bounds_.resize(stats_.chunk_count);
    chunk_has_bounds_.resize(stats_.chunk_count, 0u);
    uint32_t chunked_record_count = 0;

    for (size_t i = 0; i < scene.models.size(); ++i) {
        const auto& model_ptr = scene.models[i];
        if (!model_ptr) {
            continue;
        }

        const auto& model = *model_ptr;
        const nw::render::ModelInstanceHandle instance_handle = i < scene.model_instance_handles.size()
            ? scene.model_instance_handles[i]
            : nw::render::ModelInstanceHandle{};
        const auto* instance = scene.model_instances.get(instance_handle);
        const bool valid_instance = instance
            && instance->kind == nw::render::ModelInstanceKind::nwn_legacy
            && instance->nwn_legacy_model_index == static_cast<uint32_t>(i);
        const AreaRenderSourceInfo& info = source_info_for_model(scene, i);
        const nw::render::Bounds current_bounds = valid_instance ? instance->current_bounds : model.current_bounds();
        expand_bounds(scene_bounds_, current_bounds, has_scene_bounds_);
        const uint8_t pass_mask = model.material_pass_mask;
        const bool model_static = info.static_candidate && !model.scene_animation_enabled && !model.transform_context_;
        uint8_t flags = 0;
        if (valid_instance ? instance->visible : model.render_enabled) {
            flags |= RecordFlag::render_enabled;
        } else {
            ++stats_.disabled_record_count;
        }
        if (model_static) {
            flags |= RecordFlag::static_candidate;
            ++stats_.static_record_count;
        } else {
            ++stats_.dynamic_record_count;
        }
        const bool record_casts_shadow = valid_instance ? instance->shadow.casts_shadow : model.has_shadow_casters();
        if (record_casts_shadow) {
            flags |= RecordFlag::shadow_caster;
            ++stats_.shadow_caster_record_count;
        }
        if (has_opaque_cutout_pass(pass_mask)) {
            ++stats_.opaque_cutout_record_count;
        }
        if (has_water_pass(pass_mask)) {
            ++stats_.water_record_count;
        }
        if (has_transparent_pass(pass_mask)) {
            ++stats_.transparent_record_count;
        }

        count_kind(stats_, info.kind);
        const uint32_t chunk_id = area_chunk_id(scene, info, current_bounds);
        if (chunk_id != kInvalidChunkId && chunk_id < chunk_counts.size()) {
            ++chunk_counts[chunk_id];
            ++chunked_record_count;
            bool chunk_initialized = chunk_has_bounds_[chunk_id] != 0u;
            expand_bounds(chunk_bounds_[chunk_id], current_bounds, chunk_initialized);
            chunk_has_bounds_[chunk_id] = chunk_initialized ? 1u : 0u;
        }

        const uint32_t record_index = saturating_count(model_indices_.size());
        model_indices_.push_back(saturating_count(i));
        if (i < model_record_indices_.size()) {
            model_record_indices_[i] = record_index;
        }
        models_.push_back(&model);
        model_kinds_.push_back(nw::render::ModelInstanceKind::nwn_legacy);
        model_instance_handles_.push_back(instance_handle);
        bounds_.push_back(current_bounds);
        root_transforms_.push_back(valid_instance ? instance->root_transform : model.root_transform());
        pass_masks_.push_back(pass_mask);
        flags_.push_back(flags);
        chunk_ids_.push_back(chunk_id);
        kinds_.push_back(info.kind);
        tile_xs_.push_back(info.tile_x);
        tile_ys_.push_back(info.tile_y);

        const size_t prepared_draw_begin = prepared_draws_.size();
        if (model_static) {
            nwn::collect_prepared_draws(prepared_draws_, model, root_transforms_.back());
        }
        const size_t prepared_draw_end = prepared_draws_.size();
        prepared_draw_record_indices_.resize(prepared_draw_end, saturating_count(model_indices_.size() - 1u));
        append_area_prepared_model_draws(
            prepared_model_draws_,
            instance_handle,
            valid_instance,
            record_casts_shadow,
            saturating_count(i),
            saturating_count(prepared_draw_begin),
            saturating_count(prepared_draw_end),
            std::span<const nwn::PreparedDrawItem>{prepared_draws_});
        stats_.max_prepared_draws_per_record = std::max(
            stats_.max_prepared_draws_per_record,
            saturating_count(prepared_draw_end - prepared_draw_begin));
        prepared_draw_offsets_.push_back(saturating_count(prepared_draws_.size()));
    }

    for (size_t i = 0; i < scene.static_models.size(); ++i) {
        const auto& model_ptr = scene.static_models[i];
        if (!model_ptr) {
            continue;
        }

        const auto& model = *model_ptr;
        const nw::render::ModelInstanceHandle instance_handle = i < scene.static_model_instance_handles.size()
            ? scene.static_model_instance_handles[i]
            : nw::render::ModelInstanceHandle{};
        const auto* instance = scene.model_instances.get(instance_handle);
        const bool valid_instance = instance
            && instance->kind == nw::render::ModelInstanceKind::render_model
            && instance->render_model_index == static_cast<uint32_t>(i);
        const AreaRenderSourceInfo& info = source_info_for_render_model(scene, i);
        const nw::render::Bounds current_bounds = valid_instance ? instance->current_bounds : model.bounds;
        expand_bounds(scene_bounds_, current_bounds, has_scene_bounds_);

        const uint8_t pass_mask = render_model_pass_mask(model);
        uint8_t flags = 0;
        if (valid_instance && instance->visible) {
            flags |= RecordFlag::render_enabled;
        } else {
            ++stats_.disabled_record_count;
        }

        ++stats_.dynamic_record_count;
        const bool record_casts_shadow = valid_instance ? instance->shadow.casts_shadow : render_model_casts_shadow(model);
        if (record_casts_shadow) {
            flags |= RecordFlag::shadow_caster;
            ++stats_.shadow_caster_record_count;
        }
        if (has_opaque_cutout_pass(pass_mask)) {
            ++stats_.opaque_cutout_record_count;
        }
        if (has_water_pass(pass_mask)) {
            ++stats_.water_record_count;
        }
        if (has_transparent_pass(pass_mask)) {
            ++stats_.transparent_record_count;
        }

        count_kind(stats_, info.kind);
        const uint32_t chunk_id = area_chunk_id(scene, info, current_bounds);
        if (chunk_id != kInvalidChunkId && chunk_id < chunk_counts.size()) {
            ++chunk_counts[chunk_id];
            ++chunked_record_count;
            bool chunk_initialized = chunk_has_bounds_[chunk_id] != 0u;
            expand_bounds(chunk_bounds_[chunk_id], current_bounds, chunk_initialized);
            chunk_has_bounds_[chunk_id] = chunk_initialized ? 1u : 0u;
        }

        const uint32_t record_index = saturating_count(model_indices_.size());
        model_indices_.push_back(saturating_count(i));
        if (i < render_model_record_indices_.size()) {
            render_model_record_indices_[i] = record_index;
        }
        models_.push_back(nullptr);
        model_kinds_.push_back(nw::render::ModelInstanceKind::render_model);
        model_instance_handles_.push_back(instance_handle);
        bounds_.push_back(current_bounds);
        root_transforms_.push_back(valid_instance ? instance->root_transform : glm::mat4{1.0f});
        pass_masks_.push_back(pass_mask);
        flags_.push_back(flags);
        chunk_ids_.push_back(chunk_id);
        kinds_.push_back(info.kind);
        tile_xs_.push_back(info.tile_x);
        tile_ys_.push_back(info.tile_y);
        prepared_draw_offsets_.push_back(saturating_count(prepared_draws_.size()));
        prepared_model_draws_.instance_offsets.push_back(saturating_count(prepared_model_draws_.draws.size()));
    }

    stats_.record_count = saturating_count(model_indices_.size());
    stats_.prepared_draw_count = saturating_count(prepared_draws_.size());
    build_static_geometry(gfx);
    chunk_offsets_.resize(static_cast<size_t>(stats_.chunk_count) + 1u, 0u);
    for (uint32_t chunk_id = 0; chunk_id < stats_.chunk_count; ++chunk_id) {
        const uint32_t count = chunk_counts[chunk_id];
        if (count > 0) {
            ++stats_.nonempty_chunk_count;
            stats_.max_records_per_chunk = std::max(stats_.max_records_per_chunk, count);
        }
        chunk_offsets_[static_cast<size_t>(chunk_id) + 1u] = chunk_offsets_[chunk_id] + count;
    }

    chunk_record_indices_.resize(chunked_record_count);
    std::vector<uint32_t> write_offsets = chunk_offsets_;
    for (uint32_t record_index = 0; record_index < chunk_ids_.size(); ++record_index) {
        const uint32_t chunk_id = chunk_ids_[record_index];
        if (chunk_id == kInvalidChunkId || chunk_id >= stats_.chunk_count) {
            continue;
        }
        const uint32_t write_index = write_offsets[chunk_id]++;
        chunk_record_indices_[write_index] = record_index;
    }

    refresh_light_indices(scene);
    nw::render::collect_prepared_model_draw_ranges(prepared_model_draw_ranges_, prepared_model_draws_);
    nw::render::collect_prepared_model_surface_draws(
        prepared_model_surface_draws_,
        prepared_model_draws_,
        prepared_model_draw_ranges_);
    prepared_surface_offsets_.assign(model_indices_.size() + 1u, 0u);
    size_t surface_cursor = 0;
    for (uint32_t record_index = 0; record_index < model_indices_.size(); ++record_index) {
        prepared_surface_offsets_[record_index] = saturating_count(surface_cursor);
        while (surface_cursor < prepared_model_surface_draws_.draws.size()
            && prepared_model_surface_draws_.draws[surface_cursor].handle_index == record_index) {
            ++surface_cursor;
        }
    }
    if (!prepared_surface_offsets_.empty()) {
        prepared_surface_offsets_.back() = saturating_count(surface_cursor);
    }
    const std::span<const nw::render::PreparedModelSurfaceDraw> prepared_surfaces{
        prepared_model_surface_draws_.draws.data(),
        prepared_model_surface_draws_.draws.size()};
    rebuild_prepared_surface_indices(prepared_surface_indices_, prepared_surface_pass_offsets_, prepared_surfaces);
    shadow_prepared_surface_offsets_.assign(model_indices_.size() + 1u, 0u);
    shadow_prepared_surface_indices_.reserve(prepared_model_surface_draws_.draws.size());
    for (uint32_t record_index = 0; record_index < model_indices_.size(); ++record_index) {
        shadow_prepared_surface_offsets_[record_index] = saturating_count(shadow_prepared_surface_indices_.size());
        if (static_cast<size_t>(record_index) + 1u >= prepared_surface_offsets_.size()) {
            continue;
        }
        const uint32_t begin = prepared_surface_offsets_[record_index];
        const uint32_t end = prepared_surface_offsets_[static_cast<size_t>(record_index) + 1u];
        if (end <= begin || end > prepared_model_surface_draws_.draws.size()) {
            continue;
        }
        const size_t shadow_surface_begin = shadow_prepared_surface_indices_.size();
        bool shadow_batchable = true;
        for (uint32_t surface_index = begin; surface_index < end; ++surface_index) {
            const auto& surface = prepared_model_surface_draws_.draws[surface_index];
            if (!surface.casts_shadow) {
                continue;
            }
            const uint32_t draw_index = area_sidecar_draw_index_for_surface(*this, surface);
            if (draw_index >= prepared_draws_.size()
                || !supports_batched_shadow_draw(prepared_draws_[draw_index])
                || !area_common_records_include_sidecar_draw(*this, record_index, draw_index)) {
                shadow_batchable = false;
                break;
            }
            shadow_prepared_surface_indices_.push_back(surface_index);
        }
        if (!shadow_batchable) {
            shadow_prepared_surface_indices_.resize(shadow_surface_begin);
        }
        stats_.max_shadow_prepared_surfaces_per_record = std::max(
            stats_.max_shadow_prepared_surfaces_per_record,
            saturating_count(shadow_prepared_surface_indices_.size() - shadow_surface_begin));
    }
    if (!shadow_prepared_surface_offsets_.empty()) {
        shadow_prepared_surface_offsets_.back() = saturating_count(shadow_prepared_surface_indices_.size());
    }

    std::vector<StaticPreparedSurfaceIndex> opaque_static_indices;
    std::vector<StaticPreparedSurfaceIndex> cutout_static_indices;
    opaque_static_indices.reserve(prepared_model_surface_draws_.draws.size());
    cutout_static_indices.reserve(prepared_model_surface_draws_.draws.size());
    opaque_static_prepared_surface_indices_.reserve(prepared_model_surface_draws_.draws.size());
    cutout_static_prepared_surface_indices_.reserve(prepared_model_surface_draws_.draws.size());
    for (size_t surface_index = 0; surface_index < prepared_model_surface_draws_.draws.size(); ++surface_index) {
        const auto& surface = prepared_model_surface_draws_.draws[surface_index];
        const uint32_t sidecar_draw_index = area_sidecar_draw_index_for_surface(*this, surface);
        if (sidecar_draw_index >= prepared_draws_.size()) {
            continue;
        }
        switch (surface.material_mode) {
        case nw::render::MaterialMode::opaque:
            opaque_static_indices.push_back(StaticPreparedSurfaceIndex{
                .surface_index = saturating_count(surface_index),
                .draw_index = sidecar_draw_index,
            });
            break;
        case nw::render::MaterialMode::cutout:
            cutout_static_indices.push_back(StaticPreparedSurfaceIndex{
                .surface_index = saturating_count(surface_index),
                .draw_index = sidecar_draw_index,
            });
            break;
        case nw::render::MaterialMode::water:
        case nw::render::MaterialMode::transparent:
            break;
        }
    }
    const auto static_surface_index_less =
        [this](const StaticPreparedSurfaceIndex& lhs, const StaticPreparedSurfaceIndex& rhs) noexcept {
            const auto* lhs_draw = lhs.draw_index < prepared_draws_.size()
                ? &prepared_draws_[lhs.draw_index]
                : nullptr;
            const auto* rhs_draw = rhs.draw_index < prepared_draws_.size()
                ? &prepared_draws_[rhs.draw_index]
                : nullptr;
            return static_batched_geometry_less(lhs_draw, rhs_draw);
        };
    std::sort(opaque_static_indices.begin(), opaque_static_indices.end(), static_surface_index_less);
    std::sort(cutout_static_indices.begin(), cutout_static_indices.end(), static_surface_index_less);
    const auto append_static_indices =
        [](const std::vector<StaticPreparedSurfaceIndex>& source,
            std::vector<uint32_t>& surface_indices) {
            for (const StaticPreparedSurfaceIndex& item : source) {
                surface_indices.push_back(item.surface_index);
            }
        };
    append_static_indices(opaque_static_indices, opaque_static_prepared_surface_indices_);
    append_static_indices(cutout_static_indices, cutout_static_prepared_surface_indices_);
    static_material_prepared_surface_indices_.reserve(
        opaque_static_prepared_surface_indices_.size() + cutout_static_prepared_surface_indices_.size());
    const auto append_static_material_surfaces =
        [&](std::span<const uint32_t> surface_indices) {
            for (const uint32_t surface_index : surface_indices) {
                if (surface_index >= prepared_model_surface_draws_.draws.size()) {
                    continue;
                }
                const auto& surface = prepared_model_surface_draws_.draws[surface_index];
                const uint32_t draw_index = area_sidecar_draw_index_for_surface(*this, surface);
                if (draw_index >= prepared_draws_.size()) {
                    continue;
                }
                auto& draw = prepared_draws_[draw_index];
                if (!supports_static_material_draw_data_cache(draw)) {
                    continue;
                }
                prepared_draws_[draw_index].static_material_index =
                    saturating_count(static_material_prepared_surface_indices_.size());
                static_material_prepared_surface_indices_.push_back(surface_index);
            }
        };
    append_static_material_surfaces(opaque_static_prepared_surface_indices_);
    append_static_material_surfaces(cutout_static_prepared_surface_indices_);
    static_material_prepared_surface_signature_ =
        index_list_signature(static_material_prepared_surface_indices_);

    std::vector<StaticPreparedSurfaceIndex> shadow_static_indices;
    shadow_static_indices.reserve(shadow_prepared_surface_indices_.size());
    sorted_shadow_prepared_surface_indices_.reserve(shadow_prepared_surface_indices_.size());
    for (const uint32_t surface_index : shadow_prepared_surface_indices_) {
        if (surface_index >= prepared_model_surface_draws_.draws.size()) {
            continue;
        }
        const auto& surface = prepared_model_surface_draws_.draws[surface_index];
        const uint32_t draw_index = area_sidecar_draw_index_for_surface(*this, surface);
        if (draw_index >= prepared_draws_.size()
            || !area_common_records_include_sidecar_draw(*this, surface.handle_index, draw_index)) {
            continue;
        }
        shadow_static_indices.push_back(StaticPreparedSurfaceIndex{
            .surface_index = surface_index,
            .draw_index = draw_index,
        });
    }
    std::sort(shadow_static_indices.begin(), shadow_static_indices.end(), static_surface_index_less);
    for (const StaticPreparedSurfaceIndex& item : shadow_static_indices) {
        sorted_shadow_prepared_surface_indices_.push_back(item.surface_index);
    }
    stats_.shadow_prepared_surface_count = saturating_count(sorted_shadow_prepared_surface_indices_.size());
}

void AreaRenderScene::refresh_runtime_records(const PreviewScene& scene)
{
    const size_t record_count = std::min({
        model_indices_.size(),
        model_instance_handles_.size(),
        flags_.size(),
        bounds_.size(),
        root_transforms_.size(),
        model_kinds_.size(),
    });

    for (size_t record_index = 0; record_index < record_count; ++record_index) {
        const uint32_t model_index = model_indices_[record_index];
        const auto* instance = scene.model_instances.get(model_instance_handles_[record_index]);
        bool valid_instance = false;
        if (instance) {
            switch (model_kinds_[record_index]) {
            case nw::render::ModelInstanceKind::nwn_legacy:
                valid_instance = instance->kind == nw::render::ModelInstanceKind::nwn_legacy
                    && instance->nwn_legacy_model_index == model_index;
                break;
            case nw::render::ModelInstanceKind::render_model:
                valid_instance = instance->kind == nw::render::ModelInstanceKind::render_model
                    && instance->render_model_index == model_index;
                break;
            }
        }
        if (!valid_instance) {
            set_flag(flags_[record_index], RecordFlag::render_enabled, false);
            set_flag(flags_[record_index], RecordFlag::shadow_caster, false);
            continue;
        }

        set_flag(flags_[record_index], RecordFlag::render_enabled, instance->visible);
        if (has_flag(flags_[record_index], RecordFlag::static_candidate)) {
            continue;
        }

        bounds_[record_index] = instance->current_bounds;
        root_transforms_[record_index] = instance->root_transform;
        set_flag(flags_[record_index], RecordFlag::shadow_caster, instance->shadow.casts_shadow);
    }
}

void AreaRenderScene::refresh_light_indices(const PreviewScene& scene)
{
    light_index_offsets_.clear();
    light_indices_.clear();
    dynamic_light_indices_.clear();
    chunk_light_index_offsets_.clear();
    chunk_light_indices_.clear();
    stats_.local_light_count = saturating_count(scene.render_local_lights.size());
    stats_.light_index_count = 0;
    stats_.dynamic_light_count = 0;
    stats_.max_light_indices_per_record = 0;
    stats_.chunk_light_index_count = 0;
    stats_.max_light_indices_per_chunk = 0;

    const size_t record_count = model_indices_.size();
    dynamic_light_indices_.reserve(scene.local_lights.size());
    for (size_t light_index = 0; light_index < scene.local_lights.size(); ++light_index) {
        const auto& light = scene.local_lights[light_index];
        if (light.model_index < scene.models.size()) {
            dynamic_light_indices_.push_back(saturating_count(light_index));
        }
    }
    stats_.dynamic_light_count = saturating_count(dynamic_light_indices_.size());

    light_index_offsets_.reserve(record_count + 1u);
    light_index_offsets_.push_back(0);
    for (size_t record_index = 0; record_index < record_count; ++record_index) {
        const size_t light_index_begin = light_indices_.size();
        if (record_index < flags_.size() && record_index < bounds_.size()
            && has_flag(flags_[record_index], AreaRenderScene::RecordFlag::static_candidate)
            && !scene.render_local_lights.empty()) {
            nwn::collect_local_light_indices_for_bounds(
                light_indices_, scene.render_local_lights, bounds_[record_index]);
        }

        const size_t light_index_end = light_indices_.size();
        const uint32_t light_index_offset = saturating_count(light_index_begin);
        const uint32_t light_index_count = saturating_count(light_index_end - light_index_begin);
        if (record_index + 1u < prepared_draw_offsets_.size()) {
            const uint32_t draw_begin = prepared_draw_offsets_[record_index];
            const uint32_t draw_end = prepared_draw_offsets_[record_index + 1u];
            const uint32_t clamped_draw_end = std::min<uint32_t>(draw_end, saturating_count(prepared_draws_.size()));
            for (uint32_t draw_index = draw_begin; draw_index < clamped_draw_end; ++draw_index) {
                prepared_draws_[draw_index].light_index_offset = light_index_offset;
                prepared_draws_[draw_index].light_index_count = light_index_count;
            }
        }
        stats_.max_light_indices_per_record = std::max(
            stats_.max_light_indices_per_record,
            light_index_count);
        light_index_offsets_.push_back(saturating_count(light_indices_.size()));
    }
    stats_.light_index_count = saturating_count(light_indices_.size());

    chunk_light_index_offsets_.resize(static_cast<size_t>(stats_.chunk_count) + 1u, 0u);
    if (stats_.chunk_count > 0 && stats_.local_light_count > 0) {
        std::vector<uint32_t> light_marks(stats_.local_light_count, 0u);
        uint32_t light_generation = 1;
        chunk_light_indices_.reserve(light_indices_.size());
        for (uint32_t chunk_id = 0; chunk_id < stats_.chunk_count; ++chunk_id) {
            if (light_generation == std::numeric_limits<uint32_t>::max()) {
                std::fill(light_marks.begin(), light_marks.end(), 0u);
                light_generation = 1;
            }
            ++light_generation;

            const uint32_t begin = chunk_offsets_[chunk_id];
            const uint32_t end = chunk_offsets_[static_cast<size_t>(chunk_id) + 1u];
            const uint32_t clamped_end = std::min<uint32_t>(end, saturating_count(chunk_record_indices_.size()));
            const size_t chunk_light_begin = chunk_light_indices_.size();
            for (uint32_t offset = begin; offset < clamped_end; ++offset) {
                for (const uint32_t light_index : light_indices_for_record(chunk_record_indices_[offset])) {
                    if (light_index >= light_marks.size() || light_marks[light_index] == light_generation) {
                        continue;
                    }
                    light_marks[light_index] = light_generation;
                    chunk_light_indices_.push_back(light_index);
                }
            }
            stats_.max_light_indices_per_chunk = std::max(
                stats_.max_light_indices_per_chunk,
                saturating_count(chunk_light_indices_.size() - chunk_light_begin));
            chunk_light_index_offsets_[static_cast<size_t>(chunk_id) + 1u] = saturating_count(chunk_light_indices_.size());
        }
    }
    stats_.chunk_light_index_count = saturating_count(chunk_light_indices_.size());
}

std::span<const nwn::PreparedDrawItem> AreaRenderScene::prepared_draws_for_record(uint32_t record_index) const noexcept
{
    if (static_cast<size_t>(record_index) + 1u >= prepared_draw_offsets_.size()) {
        return {};
    }
    const uint32_t begin = prepared_draw_offsets_[record_index];
    const uint32_t end = prepared_draw_offsets_[static_cast<size_t>(record_index) + 1u];
    if (end <= begin || end > prepared_draws_.size()) {
        return {};
    }
    return std::span<const nwn::PreparedDrawItem>{
        prepared_draws_.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

std::span<const nw::render::PreparedModelDraw> AreaRenderScene::prepared_model_draws_for_record(
    uint32_t record_index) const noexcept
{
    return nw::render::prepared_model_draws_for_handle_index(prepared_model_draws_, record_index);
}

std::span<const nw::render::PreparedModelSurfaceDraw> AreaRenderScene::prepared_model_surface_draws_for_record(
    uint32_t record_index) const noexcept
{
    if (static_cast<size_t>(record_index) + 1u >= prepared_surface_offsets_.size()) {
        return {};
    }
    const uint32_t begin = prepared_surface_offsets_[record_index];
    const uint32_t end = prepared_surface_offsets_[static_cast<size_t>(record_index) + 1u];
    if (end <= begin || end > prepared_model_surface_draws_.draws.size()) {
        return {};
    }
    return std::span<const nw::render::PreparedModelSurfaceDraw>{
        prepared_model_surface_draws_.draws.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

std::span<const uint32_t> AreaRenderScene::opaque_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 0u, 1u);
}

std::span<const uint32_t> AreaRenderScene::cutout_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 1u, 2u);
}

std::span<const uint32_t> AreaRenderScene::opaque_cutout_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 0u, 2u);
}

std::span<const uint32_t> AreaRenderScene::water_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 2u, 3u);
}

std::span<const uint32_t> AreaRenderScene::transparent_prepared_surface_indices() const noexcept
{
    return prepared_surface_index_span(prepared_surface_indices_, prepared_surface_pass_offsets_, 3u, 4u);
}

std::span<const uint32_t> AreaRenderScene::light_indices_for_record(uint32_t record_index) const noexcept
{
    if (static_cast<size_t>(record_index) + 1u >= light_index_offsets_.size()) {
        return {};
    }
    const uint32_t begin = light_index_offsets_[record_index];
    const uint32_t end = light_index_offsets_[static_cast<size_t>(record_index) + 1u];
    if (end <= begin || end > light_indices_.size()) {
        return {};
    }
    return std::span<const uint32_t>{
        light_indices_.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

std::span<const uint32_t> AreaRenderScene::shadow_prepared_surface_indices_for_record(
    uint32_t record_index) const noexcept
{
    if (static_cast<size_t>(record_index) + 1u >= shadow_prepared_surface_offsets_.size()) {
        return {};
    }
    const uint32_t begin = shadow_prepared_surface_offsets_[record_index];
    const uint32_t end = shadow_prepared_surface_offsets_[static_cast<size_t>(record_index) + 1u];
    if (end <= begin || end > shadow_prepared_surface_indices_.size()) {
        return {};
    }
    return std::span<const uint32_t>{
        shadow_prepared_surface_indices_.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

std::span<const uint32_t> AreaRenderScene::light_indices_for_chunk(uint32_t chunk_id) const noexcept
{
    if (static_cast<size_t>(chunk_id) + 1u >= chunk_light_index_offsets_.size()) {
        return {};
    }
    const uint32_t begin = chunk_light_index_offsets_[chunk_id];
    const uint32_t end = chunk_light_index_offsets_[static_cast<size_t>(chunk_id) + 1u];
    if (end <= begin || end > chunk_light_indices_.size()) {
        return {};
    }
    return std::span<const uint32_t>{
        chunk_light_indices_.data() + begin,
        static_cast<size_t>(end - begin),
    };
}

size_t rebuild_area_visibility_mask(
    const AreaRenderScene& scene,
    const AreaVisibilityMaskOptions& options,
    std::vector<uint8_t>& mask)
{
    const auto& stats = scene.stats();
    const uint32_t width = stats.chunk_width;
    const uint32_t height = stats.chunk_height;
    const uint32_t chunk_count = stats.chunk_count;
    if (options.radius_tiles < 0 || width == 0 || height == 0 || chunk_count != width * height) {
        mask.clear();
        return 0;
    }

    mask.resize(chunk_count);
    std::fill(mask.begin(), mask.end(), uint8_t{0u});

    const int32_t center_x = std::clamp(
        static_cast<int32_t>(std::floor(options.camera_position.x / kAreaRenderTileSize)),
        0,
        static_cast<int32_t>(width) - 1);
    const int32_t center_y = std::clamp(
        static_cast<int32_t>(std::floor(options.camera_position.y / kAreaRenderTileSize)),
        0,
        static_cast<int32_t>(height) - 1);
    const int32_t radius = std::max(options.radius_tiles, 0);
    const int32_t min_x = std::max(center_x - radius, 0);
    const int32_t max_x = std::min(center_x + radius, static_cast<int32_t>(width) - 1);
    const int32_t min_y = std::max(center_y - radius, 0);
    const int32_t max_y = std::min(center_y + radius, static_cast<int32_t>(height) - 1);

    constexpr float kPi = 3.14159265358979323846f;
    const glm::vec2 camera_xy{options.camera_position.x, options.camera_position.y};
    glm::vec2 forward{
        options.camera_target.x - options.camera_position.x,
        options.camera_target.y - options.camera_position.y,
    };
    const float forward_length_squared = glm::dot(forward, forward);
    const bool use_view_cone = options.mode == AreaVisibilityMaskMode::view_cone
        && forward_length_squared > 1.0e-6f;
    if (use_view_cone) {
        forward /= std::sqrt(forward_length_squared);
    }
    const float half_angle = std::clamp(options.half_angle_degrees, 1.0f, 179.0f);
    const float cone_cos = std::cos(half_angle * kPi / 180.0f);
    const float near_keep_distance = kAreaRenderTileSize * 1.5f;
    const float near_keep_distance_squared = near_keep_distance * near_keep_distance;

    size_t visible_count = 0;
    for (int32_t y = min_y; y <= max_y; ++y) {
        for (int32_t x = min_x; x <= max_x; ++x) {
            bool visible = true;
            if (use_view_cone) {
                const glm::vec2 chunk_center{
                    (static_cast<float>(x) + 0.5f) * kAreaRenderTileSize,
                    (static_cast<float>(y) + 0.5f) * kAreaRenderTileSize,
                };
                const glm::vec2 delta = chunk_center - camera_xy;
                const float distance_squared = glm::dot(delta, delta);
                if (distance_squared > near_keep_distance_squared) {
                    visible = glm::dot(delta, forward) >= std::sqrt(distance_squared) * cone_cos;
                }
            }
            if (visible) {
                mask[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = 1u;
                ++visible_count;
            }
        }
    }
    return visible_count;
}

void AreaRenderFrame::clear()
{
    visible_record_indices_.clear();
    visible_chunk_indices_.clear();
    opaque_cutout_record_indices_.clear();
    water_record_indices_.clear();
    transparent_record_indices_.clear();
    shadow_caster_record_indices_.clear();
    visible_render_model_instance_handles_.clear();
    direct_all_records_.clear();
    direct_opaque_cutout_records_.clear();
    direct_water_records_.clear();
    direct_transparent_records_.clear();
    visible_light_indices_.clear();
    visible_prepared_surface_indices_.clear();
    visible_prepared_surface_pass_offsets_.fill(0u);
    visible_opaque_static_prepared_surface_indices_.clear();
    visible_cutout_static_prepared_surface_indices_.clear();
    visible_opaque_prepared_indirect_.clear();
    visible_cutout_prepared_indirect_.clear();
    visible_opaque_static_material_indirect_.clear();
    visible_cutout_static_material_indirect_.clear();
    for (auto& cascade_draws : shadow_cascade_draws_) {
        cascade_draws.clear();
        cascade_draws.clear_cache();
    }
    record_marks_.clear();
    chunk_marks_.clear();
    light_marks_.clear();
    visible_bounds_ = {};
    shadow_caster_bounds_ = {};
    cached_draw_scene_ = nullptr;
    stats_ = {};
    record_mark_generation_ = 1;
    mark_generation_ = 1;
    light_mark_generation_ = 1;
    has_visible_bounds_ = false;
    has_shadow_caster_bounds_ = false;
    filtered_light_indices_valid_ = false;
    sorted_visible_static_draw_lists_ = false;
}

void AreaRenderFrame::reserve_for_scene(const AreaRenderScene& scene)
{
    const size_t record_count = scene.model_indices().size();
    visible_record_indices_.reserve(record_count);
    visible_chunk_indices_.reserve(scene.stats().chunk_count);
    opaque_cutout_record_indices_.reserve(record_count);
    water_record_indices_.reserve(record_count);
    transparent_record_indices_.reserve(record_count);
    shadow_caster_record_indices_.reserve(record_count);
    visible_render_model_instance_handles_.reserve(record_count);
    visible_nwn_legacy_instance_handles_.reserve(record_count);
    direct_all_records_.reserve(record_count);
    direct_opaque_cutout_records_.reserve(record_count);
    direct_water_records_.reserve(record_count);
    direct_transparent_records_.reserve(record_count);
    visible_light_indices_.reserve(scene.stats().local_light_count);
    const size_t prepared_surface_count = scene.prepared_model_surface_draws().draws.size();
    visible_prepared_surface_indices_.reserve(prepared_surface_count);
    visible_opaque_static_prepared_surface_indices_.reserve(
        scene.opaque_static_prepared_surface_indices().size());
    visible_cutout_static_prepared_surface_indices_.reserve(
        scene.cutout_static_prepared_surface_indices().size());
    for (auto& cascade_draws : shadow_cascade_draws_) {
        cascade_draws.reserve(record_count, prepared_surface_count);
    }
    record_marks_.resize(record_count, 0u);
    chunk_marks_.resize(scene.stats().chunk_count, 0u);
    light_marks_.resize(scene.stats().local_light_count, 0u);
}

std::span<const uint32_t> AreaRenderFrame::visible_opaque_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->opaque_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        0u,
        1u);
}

std::span<const uint32_t> AreaRenderFrame::visible_cutout_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->cutout_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        1u,
        2u);
}

std::span<const uint32_t> AreaRenderFrame::visible_opaque_cutout_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->opaque_cutout_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        0u,
        2u);
}

std::span<const uint32_t> AreaRenderFrame::visible_water_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->water_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        2u,
        3u);
}

std::span<const uint32_t> AreaRenderFrame::visible_transparent_prepared_surface_indices() const noexcept
{
    if (cached_draw_scene_) {
        return cached_draw_scene_->transparent_prepared_surface_indices();
    }
    return prepared_surface_index_span(
        visible_prepared_surface_indices_,
        visible_prepared_surface_pass_offsets_,
        3u,
        4u);
}

const AreaDirectModelRecordSelection& AreaRenderFrame::direct_model_records_for_pass(
    nw::render::RenderPassSelection pass) const noexcept
{
    switch (pass) {
    case nw::render::RenderPassSelection::opaque_cutout:
        return direct_opaque_cutout_records_;
    case nw::render::RenderPassSelection::water:
        return direct_water_records_;
    case nw::render::RenderPassSelection::transparent:
        return direct_transparent_records_;
    case nw::render::RenderPassSelection::all:
        return direct_all_records_;
    }
    return direct_all_records_;
}

void prepare_area_frame(const AreaRenderScene& scene, AreaRenderFrame& frame, const AreaRenderCullContext& cull)
{
    frame.reserve_for_scene(scene);
    frame.visible_record_indices_.clear();
    frame.visible_chunk_indices_.clear();
    frame.opaque_cutout_record_indices_.clear();
    frame.water_record_indices_.clear();
    frame.transparent_record_indices_.clear();
    frame.shadow_caster_record_indices_.clear();
    frame.visible_render_model_instance_handles_.clear();
    frame.visible_nwn_legacy_instance_handles_.clear();
    frame.direct_all_records_.clear();
    frame.direct_opaque_cutout_records_.clear();
    frame.direct_water_records_.clear();
    frame.direct_transparent_records_.clear();
    frame.visible_light_indices_.clear();
    frame.visible_prepared_surface_indices_.clear();
    frame.visible_prepared_surface_pass_offsets_.fill(0u);
    frame.visible_opaque_static_prepared_surface_indices_.clear();
    frame.visible_cutout_static_prepared_surface_indices_.clear();
    for (auto& cascade_draws : frame.shadow_cascade_draws_) {
        cascade_draws.clear();
    }
    frame.cached_draw_scene_ = nullptr;
    frame.visible_bounds_ = {};
    frame.shadow_caster_bounds_ = {};
    frame.has_visible_bounds_ = false;
    frame.has_shadow_caster_bounds_ = false;
    frame.filtered_light_indices_valid_ = false;
    frame.sorted_visible_static_draw_lists_ = false;
    frame.stats_ = {};

    if (frame.record_mark_generation_ == std::numeric_limits<uint32_t>::max()) {
        std::fill(frame.record_marks_.begin(), frame.record_marks_.end(), 0u);
        frame.record_mark_generation_ = 1;
    }
    ++frame.record_mark_generation_;
    if (frame.mark_generation_ == std::numeric_limits<uint32_t>::max()) {
        std::fill(frame.chunk_marks_.begin(), frame.chunk_marks_.end(), 0u);
        frame.mark_generation_ = 1;
    }
    ++frame.mark_generation_;
    if (frame.light_mark_generation_ == std::numeric_limits<uint32_t>::max()) {
        std::fill(frame.light_marks_.begin(), frame.light_marks_.end(), 0u);
        frame.light_mark_generation_ = 1;
    }
    ++frame.light_mark_generation_;

    const auto record_indices = scene.model_indices();
    const auto pass_masks = scene.pass_masks();
    const auto flags = scene.flags();
    const auto chunk_ids = scene.chunk_ids();
    const auto bounds = scene.bounds();
    const std::span<const nw::render::PreparedModelSurfaceDraw> prepared_surfaces{
        scene.prepared_model_surface_draws().draws.data(),
        scene.prepared_model_surface_draws().draws.size()};
    const auto chunk_bounds = scene.chunk_bounds();
    const auto chunk_has_bounds = scene.chunk_has_bounds();
    const uint32_t chunk_count = scene.stats().chunk_count;
    const bool chunk_visibility_enabled = cull.chunk_visibility_enabled
        && cull.visible_chunk_mask.size() >= chunk_count;
    const bool cull_enabled = cull.enabled
        && chunk_count > 0
        && chunk_bounds.size() >= chunk_count
        && chunk_has_bounds.size() >= chunk_count
        && scene.chunk_offsets().size() > chunk_count;
    const AreaRenderFrustum frustum = cull_enabled ? make_area_render_frustum(cull.view_projection) : AreaRenderFrustum{};

    const auto record_bounds = [&](uint32_t record_index) -> Bounds {
        if (record_index < bounds.size()) {
            return bounds[record_index];
        }
        return {};
    };

    const auto append_visible_record = [&](uint32_t record_index) {
        if (record_index >= record_indices.size() || record_index >= flags.size() || record_index >= chunk_ids.size()) {
            return;
        }
        if (!has_flag(flags[record_index], AreaRenderScene::RecordFlag::render_enabled)) {
            return;
        }

        const Bounds current_bounds = record_bounds(record_index);
        frame.visible_record_indices_.push_back(record_index);
        if (record_index < frame.record_marks_.size()) {
            frame.record_marks_[record_index] = frame.record_mark_generation_;
        }
        expand_bounds(frame.visible_bounds_, current_bounds, frame.has_visible_bounds_);
        if (has_flag(flags[record_index], AreaRenderScene::RecordFlag::static_candidate)) {
            ++frame.stats_.visible_static_record_count;
            const auto surfaces = scene.prepared_model_surface_draws_for_record(record_index);
            frame.stats_.visible_prepared_surface_count += saturating_count(surfaces.size());
        } else {
            ++frame.stats_.visible_dynamic_record_count;
        }

        const uint32_t chunk_id = chunk_ids[record_index];
        if (chunk_id < frame.chunk_marks_.size() && frame.chunk_marks_[chunk_id] != frame.mark_generation_) {
            frame.chunk_marks_[chunk_id] = frame.mark_generation_;
            frame.visible_chunk_indices_.push_back(chunk_id);
            ++frame.stats_.visible_chunk_count;
        }
    };

    const auto append_record_if_visible = [&](uint32_t record_index) {
        if (frustum.valid && record_index < bounds.size()
            && !bounds_intersects_frustum(bounds[record_index], frustum)) {
            ++frame.stats_.culled_record_count;
            return;
        }
        append_visible_record(record_index);
    };

    if (cull_enabled && frustum.valid) {
        const auto chunk_offsets = scene.chunk_offsets();
        const auto chunk_record_indices = scene.chunk_record_indices();
        for (uint32_t chunk_id = 0; chunk_id < chunk_count; ++chunk_id) {
            const uint32_t begin = chunk_offsets[chunk_id];
            const uint32_t end = chunk_offsets[static_cast<size_t>(chunk_id) + 1u];
            if (begin >= end || begin >= chunk_record_indices.size()) {
                continue;
            }

            const uint32_t clamped_end = std::min<uint32_t>(end, saturating_count(chunk_record_indices.size()));
            if (chunk_visibility_enabled && cull.visible_chunk_mask[chunk_id] == 0u) {
                ++frame.stats_.culled_chunk_count;
                ++frame.stats_.visibility_culled_chunk_count;
                const uint32_t culled_records = clamped_end - begin;
                frame.stats_.culled_record_count += culled_records;
                frame.stats_.visibility_culled_record_count += culled_records;
                continue;
            }
            if (chunk_has_bounds[chunk_id] != 0u
                && !bounds_intersects_frustum(chunk_bounds[chunk_id], frustum)) {
                ++frame.stats_.culled_chunk_count;
                frame.stats_.culled_record_count += clamped_end - begin;
                continue;
            }

            for (uint32_t offset = begin; offset < clamped_end; ++offset) {
                append_record_if_visible(chunk_record_indices[offset]);
            }
        }

        for (uint32_t record_index = 0; record_index < chunk_ids.size(); ++record_index) {
            const uint32_t chunk_id = chunk_ids[record_index];
            if (chunk_id == kInvalidChunkId || chunk_id >= chunk_count) {
                append_record_if_visible(record_index);
            }
        }
    } else {
        for (uint32_t record_index = 0; record_index < record_indices.size(); ++record_index) {
            append_visible_record(record_index);
        }
    }

    frame.stats_.visible_record_count = saturating_count(frame.visible_record_indices_.size());
    const bool use_cached_draw_lists = scene.stats().disabled_record_count == 0
        && frame.stats_.visible_static_record_count == scene.stats().static_record_count;
    frame.stats_.uses_cached_draw_lists = use_cached_draw_lists;
    if (use_cached_draw_lists) {
        frame.cached_draw_scene_ = &scene;
    }
    const bool use_sorted_visible_static_draws = !use_cached_draw_lists
        && should_use_sorted_area_static_surface_lists(
            frame.stats_.visible_prepared_surface_count,
            saturating_count(prepared_surfaces.size()));

    const bool build_filtered_lights = scene.stats().local_light_count > 0
        && frame.stats_.visible_record_count < scene.stats().record_count;
    if (build_filtered_lights) {
        frame.filtered_light_indices_valid_ = true;
        const auto append_light_index = [&](uint32_t light_index) {
            if (light_index >= frame.light_marks_.size()
                || frame.light_marks_[light_index] == frame.light_mark_generation_) {
                return;
            }
            frame.light_marks_[light_index] = frame.light_mark_generation_;
            frame.visible_light_indices_.push_back(light_index);
        };

        for (const uint32_t chunk_id : frame.visible_chunk_indices_) {
            for (const uint32_t light_index : scene.light_indices_for_chunk(chunk_id)) {
                append_light_index(light_index);
            }
        }

        for (const uint32_t record_index : frame.visible_record_indices_) {
            if (record_index >= chunk_ids.size()) {
                continue;
            }
            const uint32_t chunk_id = chunk_ids[record_index];
            if (chunk_id != kInvalidChunkId && chunk_id < chunk_count) {
                continue;
            }
            for (const uint32_t light_index : scene.light_indices_for_record(record_index)) {
                append_light_index(light_index);
            }
        }
        for (const uint32_t light_index : scene.dynamic_light_indices()) {
            append_light_index(light_index);
        }
        frame.stats_.visible_light_count = saturating_count(frame.visible_light_indices_.size());
    } else {
        frame.stats_.visible_light_count = scene.stats().local_light_count;
    }

    for (const uint32_t record_index : frame.visible_record_indices_) {
        if (record_index >= pass_masks.size() || record_index >= flags.size()) {
            continue;
        }

        const uint8_t pass_mask = pass_masks[record_index];
        append_area_visible_render_model_handle(
            frame.visible_render_model_instance_handles_,
            scene,
            record_index);
        append_area_visible_nwn_legacy_handle(
            frame.visible_nwn_legacy_instance_handles_,
            scene,
            record_index);
        append_area_direct_model_record(
            frame.direct_all_records_,
            scene,
            record_index,
            cull.collect_direct_render_model_records);
        if (has_opaque_cutout_pass(pass_mask)) {
            frame.opaque_cutout_record_indices_.push_back(record_index);
            append_area_direct_model_record(
                frame.direct_opaque_cutout_records_,
                scene,
                record_index,
                cull.collect_direct_render_model_records);
        }
        if (has_water_pass(pass_mask)) {
            frame.water_record_indices_.push_back(record_index);
            append_area_direct_model_record(
                frame.direct_water_records_,
                scene,
                record_index,
                cull.collect_direct_render_model_records);
        }
        if (has_transparent_pass(pass_mask)) {
            frame.transparent_record_indices_.push_back(record_index);
            append_area_direct_model_record(
                frame.direct_transparent_records_,
                scene,
                record_index,
                cull.collect_direct_render_model_records);
        }
        if (has_flag(flags[record_index], AreaRenderScene::RecordFlag::shadow_caster)) {
            frame.shadow_caster_record_indices_.push_back(record_index);
            expand_bounds(
                frame.shadow_caster_bounds_,
                record_bounds(record_index),
                frame.has_shadow_caster_bounds_);
        }

        if (use_cached_draw_lists || use_sorted_visible_static_draws
            || !has_flag(flags[record_index], AreaRenderScene::RecordFlag::static_candidate)) {
            continue;
        }
        const auto surfaces = scene.prepared_model_surface_draws_for_record(record_index);
        for (const auto& surface : surfaces) {
            const size_t surface_index = static_cast<size_t>(&surface - prepared_surfaces.data());
            if (surface_index < prepared_surfaces.size()) {
                frame.visible_prepared_surface_indices_.push_back(saturating_count(surface_index));
            }
        }
    }

    if (use_sorted_visible_static_draws) {
        for (const uint32_t surface_index : scene.prepared_surface_indices()) {
            if (surface_index >= prepared_surfaces.size()) {
                continue;
            }
            const uint32_t record_index = prepared_surfaces[surface_index].handle_index;
            if (record_index < frame.record_marks_.size()
                && frame.record_marks_[record_index] == frame.record_mark_generation_) {
                frame.visible_prepared_surface_indices_.push_back(surface_index);
            }
        }

        append_visible_legacy_static_prepared_surface_indices(
            frame.visible_opaque_static_prepared_surface_indices_,
            scene,
            scene.opaque_static_prepared_surface_indices(),
            prepared_surfaces,
            frame.record_marks_,
            frame.record_mark_generation_);
        append_visible_legacy_static_prepared_surface_indices(
            frame.visible_cutout_static_prepared_surface_indices_,
            scene,
            scene.cutout_static_prepared_surface_indices(),
            prepared_surfaces,
            frame.record_marks_,
            frame.record_mark_generation_);
        if (!frame.visible_opaque_static_prepared_surface_indices_.empty()
            || !frame.visible_cutout_static_prepared_surface_indices_.empty()) {
            frame.sorted_visible_static_draw_lists_ = true;
        }
    }
    if (!use_cached_draw_lists) {
        if (!use_sorted_visible_static_draws) {
            sort_prepared_surface_indices(frame.visible_prepared_surface_indices_, prepared_surfaces);
        }
        rebuild_prepared_surface_pass_offsets(
            frame.visible_prepared_surface_pass_offsets_,
            frame.visible_prepared_surface_indices_,
            prepared_surfaces);
    }
    frame.stats_.uses_sorted_static_draw_lists = frame.uses_sorted_static_draw_lists();
    if (frame.sorted_visible_static_draw_lists_) {
        refresh_prepared_indirect_cache_for_surfaces(
            frame.visible_opaque_prepared_indirect_,
            scene,
            frame.visible_opaque_static_prepared_surface_indices_,
            nw::render::MaterialMode::opaque,
            &frame.stats_.material_indirect_sidecar_bridge);
        refresh_prepared_indirect_cache_for_surfaces(
            frame.visible_cutout_prepared_indirect_,
            scene,
            frame.visible_cutout_static_prepared_surface_indices_,
            nw::render::MaterialMode::cutout,
            &frame.stats_.material_indirect_sidecar_bridge);
        refresh_prepared_static_material_indirect_cache_for_surfaces(
            frame.visible_opaque_static_material_indirect_,
            scene,
            frame.visible_opaque_static_prepared_surface_indices_,
            nw::render::MaterialMode::opaque,
            &frame.stats_.static_material_indirect_sidecar_bridge);
        refresh_prepared_static_material_indirect_cache_for_surfaces(
            frame.visible_cutout_static_material_indirect_,
            scene,
            frame.visible_cutout_static_prepared_surface_indices_,
            nw::render::MaterialMode::cutout,
            &frame.stats_.static_material_indirect_sidecar_bridge);
    }

    frame.stats_.opaque_cutout_record_count = saturating_count(frame.opaque_cutout_record_indices_.size());
    frame.stats_.water_record_count = saturating_count(frame.water_record_indices_.size());
    frame.stats_.transparent_record_count = saturating_count(frame.transparent_record_indices_.size());
    frame.stats_.shadow_caster_record_count = saturating_count(frame.shadow_caster_record_indices_.size());
}

namespace {

template <typename SubmitLegacyModel>
AreaDirectModelSubmissionStats collect_area_direct_legacy_model_records_impl(
    SubmitLegacyModel&& submit_legacy_model,
    const AreaRenderScene& area_scene,
    const AreaDirectModelRecordSelection& records,
    AreaDirectRenderModelRecordPolicy render_model_policy)
{
    AreaDirectModelSubmissionStats stats{};
    stats.input_record_count = saturating_count(records.records.size());

    const auto model_indices = area_scene.model_indices();
    const auto roots = area_scene.root_transforms();
    const auto legacy_models = area_scene.models();

    for (const auto& record : records.records) {
        if (record.kind == nw::render::ModelInstanceKind::render_model
            && render_model_policy == AreaDirectRenderModelRecordPolicy::ignore) {
            continue;
        }

        const uint32_t record_index = record.record_index;
        if (record_index >= model_indices.size()) {
            add_saturating(stats.dropped_invalid_record_count, 1u);
            continue;
        }

        const uint32_t model_index = model_indices[record_index];
        switch (record.kind) {
        case nw::render::ModelInstanceKind::nwn_legacy: {
            if (record_index >= legacy_models.size()
                || area_scene.record_index_for_model(model_index) != record_index) {
                add_saturating(stats.dropped_invalid_record_count, 1u);
                continue;
            }

            const auto* model = legacy_models[record_index];
            if (record_uses_static_prepared_legacy_path(area_scene, record_index) || !model) {
                add_saturating(stats.dropped_invalid_source_count, 1u);
                continue;
            }

            const glm::mat4 root = record_index < roots.size()
                ? roots[record_index]
                : model->root_transform();
            add_saturating(stats.selected_legacy_record_count, 1u);
            submit_legacy_model(*model, root);
            break;
        }
        case nw::render::ModelInstanceKind::render_model: {
            if (render_model_policy == AreaDirectRenderModelRecordPolicy::count_skipped) {
                add_saturating(stats.skipped_render_model_record_count, 1u);
            }
            break;
        }
        default:
            add_saturating(stats.dropped_unsupported_kind_count, 1u);
            break;
        }
    }

    return stats;
}

} // namespace

AreaDirectModelSubmissionStats collect_area_direct_legacy_model_records(
    const AreaRenderScene& area_scene,
    const AreaDirectModelRecordSelection& records,
    AreaDirectRenderModelRecordPolicy render_model_policy)
{
    const auto submit_legacy_model = [](const auto&, const glm::mat4&) {};
    return collect_area_direct_legacy_model_records_impl(
        submit_legacy_model,
        area_scene,
        records,
        render_model_policy);
}

AreaDirectModelSubmissionStats render_area_direct_legacy_model_records(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const AreaRenderScene& area_scene,
    const AreaDirectModelRecordSelection& records,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    AreaDirectRenderModelRecordPolicy render_model_policy)
{
    std::optional<nw::render::nwn::ModelRenderContext> nwn_render_ctx{};
    const auto legacy_context = [&]() -> nw::render::nwn::ModelRenderContext& {
        if (!nwn_render_ctx) {
            nwn_render_ctx = render_service.nwn_model_render_context();
        }
        return *nwn_render_ctx;
    };
    const auto submit_legacy_model = [&](const nw::render::nwn::ModelInstance& model, const glm::mat4& root) {
        if (!cmd) {
            return;
        }
        nw::render::nwn::render_model_instance_with_root(
            legacy_context(),
            cmd,
            model,
            root,
            ctx,
            pass);
    };
    return collect_area_direct_legacy_model_records_impl(
        submit_legacy_model,
        area_scene,
        records,
        render_model_policy);
}

AreaDirectModelSubmissionStats render_area_direct_render_model_records(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    const AreaRenderScene& area_scene,
    const AreaDirectModelRecordSelection& records,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass)
{
    AreaDirectModelSubmissionStats stats{};
    stats.input_record_count = saturating_count(records.records.size());

    const auto model_indices = area_scene.model_indices();
    const auto roots = area_scene.root_transforms();

    for (const auto& record : records.records) {
        if (record.kind != nw::render::ModelInstanceKind::render_model) {
            continue;
        }

        const uint32_t record_index = record.record_index;
        if (record_index >= model_indices.size()) {
            add_saturating(stats.dropped_invalid_record_count, 1u);
            continue;
        }

        const uint32_t model_index = model_indices[record_index];
        if (area_scene.record_index_for_render_model(model_index) != record_index
            || model_index >= scene.static_models.size()) {
            add_saturating(stats.dropped_invalid_record_count, 1u);
            continue;
        }

        const auto& model = scene.static_models[model_index];
        const auto* instance = scene.static_model_instance(model_index);
        if (!model || !instance || !instance->visible) {
            add_saturating(stats.dropped_invalid_source_count, 1u);
            continue;
        }

        const glm::mat4 root = record_index < roots.size()
            ? roots[record_index]
            : instance->root_transform;
        add_saturating(stats.selected_render_model_record_count, 1u);
        nw::render::render_render_model_with_root(
            render_model_ctx,
            cmd,
            *model,
            root,
            ctx,
            pass,
            instance);
    }

    return stats;
}

std::string_view area_render_record_kind_label(AreaRenderRecordKind kind) noexcept
{
    switch (kind) {
    case AreaRenderRecordKind::tile:
        return "tile";
    case AreaRenderRecordKind::creature:
        return "creature";
    case AreaRenderRecordKind::door:
        return "door";
    case AreaRenderRecordKind::item:
        return "item";
    case AreaRenderRecordKind::placeable:
        return "placeable";
    case AreaRenderRecordKind::unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace nw::render::viewer
