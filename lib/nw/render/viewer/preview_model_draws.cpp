#include "preview_model_draws.hpp"

#include "area_render_scene.hpp"
#include "preview_model_animation.hpp"
#include "preview_scene.hpp"

#include <nw/render/model_renderer.hpp>

#include <absl/container/flat_hash_map.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace nw::render::viewer {

namespace {

uint32_t saturating_count(size_t value) noexcept
{
    return value > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(value);
}

void add_saturating(uint32_t& target, uint32_t value) noexcept
{
    const uint64_t next = static_cast<uint64_t>(target) + static_cast<uint64_t>(value);
    target = static_cast<uint32_t>(std::min<uint64_t>(next, std::numeric_limits<uint32_t>::max()));
}

void add_protocol_mismatch(PreviewPreparedModelDrawValidation& validation) noexcept
{
    add_saturating(validation.protocol_mismatch_count, 1u);
}

void add_material_stat(PreviewPreparedModelDrawMaterialStats& stats, nw::render::MaterialMode mode) noexcept
{
    switch (mode) {
    case nw::render::MaterialMode::opaque:
        ++stats.opaque_count;
        break;
    case nw::render::MaterialMode::cutout:
        ++stats.cutout_count;
        break;
    case nw::render::MaterialMode::transparent:
        ++stats.transparent_count;
        break;
    case nw::render::MaterialMode::water:
        ++stats.water_count;
        break;
    default:
        ++stats.invalid_mode_count;
        break;
    }
}

bool material_stats_equal(
    const PreviewPreparedModelDrawMaterialStats& lhs,
    const PreviewPreparedModelDrawMaterialStats& rhs) noexcept
{
    return lhs.opaque_count == rhs.opaque_count
        && lhs.cutout_count == rhs.cutout_count
        && lhs.transparent_count == rhs.transparent_count
        && lhs.water_count == rhs.water_count
        && lhs.invalid_mode_count == rhs.invalid_mode_count;
}

void compare_stat(uint32_t expected, uint32_t prepared, PreviewPreparedModelDrawValidation& validation) noexcept
{
    if (expected != prepared) {
        add_protocol_mismatch(validation);
    }
}

void compare_prepared_stats(PreviewPreparedModelDrawValidation& validation) noexcept
{
    const auto& expected = validation.expected_stats;
    const auto& prepared = validation.prepared_stats;
    compare_stat(expected.handle_count, prepared.handle_count, validation);
    compare_stat(expected.visible_instance_count, prepared.visible_instance_count, validation);
    compare_stat(expected.hidden_instance_count, prepared.hidden_instance_count, validation);
    compare_stat(expected.stale_handle_count, prepared.stale_handle_count, validation);
    compare_stat(expected.missing_asset_count, prepared.missing_asset_count, validation);
    compare_stat(expected.invalid_draw_count, prepared.invalid_draw_count, validation);
    compare_stat(expected.render_model_instance_count, prepared.render_model_instance_count, validation);
    compare_stat(expected.render_model_draw_count, prepared.render_model_draw_count, validation);
    compare_stat(expected.material_fallback_draw_count, prepared.material_fallback_draw_count, validation);
    compare_stat(
        expected.render_model_material_fallback_draw_count,
        prepared.render_model_material_fallback_draw_count,
        validation);
    compare_stat(expected.material_override_draw_count, prepared.material_override_draw_count, validation);
    compare_stat(
        expected.invalid_material_override_handle_count,
        prepared.invalid_material_override_handle_count,
        validation);
}

nw::render::ModelMaterialOverrideHandle valid_material_override_handle(
    const nw::render::ModelInstance& instance,
    const nw::render::ModelMaterialOverrideStore& material_overrides,
    uint32_t material_index) noexcept
{
    if (material_index >= instance.material_override_handles.size()) {
        return {};
    }

    const auto handle = instance.material_override_handles[material_index];
    return material_overrides.valid(handle) ? handle : nw::render::ModelMaterialOverrideHandle{};
}

nw::render::ModelMaterialOverrideHandle collect_material_override_handle(
    const nw::render::ModelInstance& instance,
    const nw::render::ModelMaterialOverrideStore& material_overrides,
    uint32_t material_index,
    nw::render::PreparedModelDrawStats& stats) noexcept
{
    if (material_index >= instance.material_override_handles.size()) {
        return {};
    }

    const auto handle = instance.material_override_handles[material_index];
    if (!handle.valid()) {
        return {};
    }
    if (!material_overrides.valid(handle)) {
        add_saturating(stats.invalid_material_override_handle_count, 1u);
        return {};
    }

    add_saturating(stats.material_override_draw_count, 1u);
    return handle;
}

nw::render::Bounds transform_bounds(const nw::render::Bounds& bounds, const glm::mat4& transform)
{
    const std::array<glm::vec3, 8> corners{
        glm::vec3{bounds.min.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.min.z},
        glm::vec3{bounds.min.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.min.y, bounds.max.z},
        glm::vec3{bounds.min.x, bounds.max.y, bounds.max.z},
        glm::vec3{bounds.max.x, bounds.max.y, bounds.max.z},
    };

    nw::render::Bounds result{
        .min = glm::vec3{std::numeric_limits<float>::max()},
        .max = glm::vec3{std::numeric_limits<float>::lowest()},
    };
    for (const auto& corner : corners) {
        const glm::vec3 transformed = glm::vec3(transform * glm::vec4(corner, 1.0f));
        result.min = glm::min(result.min, transformed);
        result.max = glm::max(result.max, transformed);
    }
    return result;
}

glm::mat4 make_normal_matrix(const glm::mat4& model_matrix)
{
    return glm::mat4(glm::mat3(glm::transpose(glm::inverse(model_matrix))));
}

void append_instance_offset(PreviewPreparedModelDraws& out)
{
    out.common.instance_offsets.push_back(saturating_count(out.common.draws.size()));
}

void append_render_model_draws_impl(
    nw::render::PreparedModelDrawList& out,
    const nw::render::ModelInstanceHandle handle,
    const nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    const nw::render::ModelMaterialOverrideStore& material_overrides,
    std::vector<PreviewPreparedModelDraws::Transform>* transform_cache = nullptr)
{
    auto& stats = out.stats;
    ++stats.render_model_instance_count;
    for (size_t primitive_index = 0; primitive_index < model.primitives.size(); ++primitive_index) {
        const auto& primitive = model.primitives[primitive_index];
        if (primitive.index_count == 0 || primitive.material >= model.materials.size()) {
            ++stats.invalid_draw_count;
            continue;
        }

        const auto& material = model.materials[primitive.material];
        const auto material_override = collect_material_override_handle(
            instance,
            material_overrides,
            primitive.material,
            stats);
        const auto* override = material_overrides.get(material_override);
        const auto& draw_material = override ? override->material : material;
        const glm::mat4 world = nw::render::model_instance_primitive_world_transform(
            &instance,
            instance.root_transform,
            primitive);
        glm::mat4 normal_matrix;
        nw::render::Bounds world_bounds;
        const size_t row = out.draws.size();
        if (transform_cache && row < transform_cache->size()
            && std::memcmp(&world, &(*transform_cache)[row].world, sizeof(world)) == 0
            && std::memcmp(&primitive.bounds, &(*transform_cache)[row].local_bounds, sizeof(primitive.bounds)) == 0) {
            normal_matrix = (*transform_cache)[row].normal_matrix;
            world_bounds = (*transform_cache)[row].world_bounds;
        } else {
            normal_matrix = make_normal_matrix(world);
            world_bounds = transform_bounds(primitive.bounds, world);
            if (transform_cache) {
                const PreviewPreparedModelDraws::Transform transform{
                    world, primitive.bounds, normal_matrix, world_bounds};
                if (row == transform_cache->size()) {
                    transform_cache->push_back(transform);
                } else {
                    (*transform_cache)[row] = transform;
                }
            }
        }
        out.draws.push_back(nw::render::PreparedModelDraw{
            .instance = handle,
            .instance_source_index = instance.render_model_index,
            .source_draw_index = saturating_count(primitive_index),
            .material_index = primitive.material,
            .material_override = material_override,
            .skin_index = primitive.skin,
            .material_mode = draw_material.alpha_mode,
            .material_uses_fallback = draw_material.material_uses_fallback,
            .material_payload = nw::render::prepared_render_model_material_payload_kind(draw_material),
            .skinned = primitive.skinned,
            .instance_casts_shadow = instance.shadow.casts_shadow,
            .primitive_casts_shadow = primitive.casts_shadow,
            .world = world,
            .normal_matrix = normal_matrix,
            .bounds = world_bounds,
        });
        ++stats.render_model_draw_count;
        if (draw_material.material_uses_fallback) {
            add_saturating(stats.material_fallback_draw_count, 1u);
            add_saturating(stats.render_model_material_fallback_draw_count, 1u);
        }
    }
}

void append_prepared_model_draws(
    PreviewPreparedModelDraws& out,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    out.common.instance_offsets.reserve(out.common.instance_offsets.size() + handles.size());
    out.common.draws.reserve(out.common.draws.size() + handles.size());
    for (const auto handle : handles) {
        ++out.common.stats.handle_count;
        const auto* instance = scene.model_instances.get(handle);
        if (!instance) {
            ++out.common.stats.stale_handle_count;
            append_instance_offset(out);
            continue;
        }
        if (!instance->visible) {
            ++out.common.stats.hidden_instance_count;
            append_instance_offset(out);
            continue;
        }

        ++out.common.stats.visible_instance_count;
        if (instance->render_model_index >= scene.static_models.size()
            || !scene.static_models[instance->render_model_index]) {
            ++out.common.stats.missing_asset_count;
            append_instance_offset(out);
            continue;
        }
        append_render_model_draws_impl(
            out.common,
            handle,
            *instance,
            *scene.static_models[instance->render_model_index],
            scene.material_overrides,
            &out.transform_cache);
        append_instance_offset(out);
    }
}

void append_expected_render_model_draws(
    PreviewPreparedModelDrawValidation& validation,
    const nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    const nw::render::ModelMaterialOverrideStore& material_overrides)
{
    auto& stats = validation.expected_stats;
    ++stats.render_model_instance_count;
    for (const auto& primitive : model.primitives) {
        if (primitive.index_count == 0 || primitive.material >= model.materials.size()) {
            ++stats.invalid_draw_count;
            continue;
        }

        ++stats.render_model_draw_count;
        const auto material_override = collect_material_override_handle(
            instance,
            material_overrides,
            primitive.material,
            stats);
        const auto& material = model.materials[primitive.material];
        const auto* override = material_overrides.get(material_override);
        const auto& draw_material = override ? override->material : material;
        if (draw_material.material_uses_fallback) {
            add_saturating(stats.material_fallback_draw_count, 1u);
            add_saturating(stats.render_model_material_fallback_draw_count, 1u);
        }
        add_material_stat(validation.expected_materials, draw_material.alpha_mode);
    }
}

void append_expected_model_draws(
    PreviewPreparedModelDrawValidation& validation,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    for (const auto handle : handles) {
        ++validation.expected_stats.handle_count;
        const auto* instance = scene.model_instances.get(handle);
        if (!instance) {
            ++validation.expected_stats.stale_handle_count;
            continue;
        }
        if (!instance->visible) {
            ++validation.expected_stats.hidden_instance_count;
            continue;
        }

        ++validation.expected_stats.visible_instance_count;
        if (instance->render_model_index >= scene.static_models.size()
            || !scene.static_models[instance->render_model_index]) {
            ++validation.expected_stats.missing_asset_count;
            continue;
        }
        append_expected_render_model_draws(
            validation,
            *instance,
            *scene.static_models[instance->render_model_index],
            scene.material_overrides);
    }
}

void validate_instance_offsets(
    PreviewPreparedModelDrawValidation& validation,
    const PreviewPreparedModelDraws& prepared)
{
    const auto& offsets = prepared.common.instance_offsets;
    const uint32_t expected_offset_count = validation.expected_stats.handle_count == std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : validation.expected_stats.handle_count + 1u;
    if (validation.instance_offset_count != expected_offset_count) {
        add_protocol_mismatch(validation);
    }
    if (offsets.empty()) {
        add_protocol_mismatch(validation);
        return;
    }
    if (offsets.front() != 0u) {
        add_protocol_mismatch(validation);
    }

    uint32_t previous = offsets.front();
    for (const uint32_t offset : offsets) {
        if (offset < previous || offset > validation.prepared_draw_count) {
            add_protocol_mismatch(validation);
        }
        previous = offset;
    }
    if (offsets.back() != validation.prepared_draw_count) {
        add_protocol_mismatch(validation);
    }
}

void validate_render_model_draw(
    PreviewPreparedModelDrawValidation& validation,
    const PreviewScene& scene,
    const nw::render::PreparedModelDraw& draw,
    const nw::render::ModelInstance& instance)
{
    if (draw.instance_source_index != instance.render_model_index
        || instance.render_model_index >= scene.static_models.size()
        || !scene.static_models[instance.render_model_index]) {
        add_protocol_mismatch(validation);
        return;
    }

    const auto& model = *scene.static_models[instance.render_model_index];
    if (draw.source_draw_index >= model.primitives.size()) {
        add_protocol_mismatch(validation);
        return;
    }

    const auto& primitive = model.primitives[draw.source_draw_index];
    if (primitive.index_count == 0 || primitive.material >= model.materials.size()) {
        add_protocol_mismatch(validation);
        return;
    }

    const auto& material = model.materials[primitive.material];
    const auto material_override = valid_material_override_handle(
        instance,
        scene.material_overrides,
        primitive.material);
    const auto* override = scene.material_overrides.get(material_override);
    const auto& draw_material = override ? override->material : material;
    if (draw.material_index != primitive.material || draw.material_mode != draw_material.alpha_mode
        || draw.material_uses_fallback != draw_material.material_uses_fallback
        || draw.material_payload != nw::render::prepared_render_model_material_payload_kind(draw_material)
        || draw.material_override != material_override
        || draw.skinned != primitive.skinned
        || draw.primitive_casts_shadow != primitive.casts_shadow) {
        add_protocol_mismatch(validation);
    }
}

void validate_prepared_draw_records(
    PreviewPreparedModelDrawValidation& validation,
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared)
{
    nw::render::PreparedModelDrawStats draw_stats{};
    for (const auto& draw : prepared.common.draws) {
        const auto* instance = scene.model_instances.get(draw.instance);
        if (!instance) {
            add_protocol_mismatch(validation);
            continue;
        }

        add_material_stat(validation.prepared_materials, draw.material_mode);
        ++draw_stats.render_model_draw_count;
        validate_render_model_draw(validation, scene, draw, *instance);
    }

    compare_stat(draw_stats.render_model_draw_count, validation.prepared_stats.render_model_draw_count, validation);
    compare_stat(validation.prepared_stats.render_model_draw_count, validation.prepared_draw_count, validation);
}

void collect_sorted_surface_draws(
    nw::render::PreparedModelSurfaceDrawList& surfaces,
    const PreviewPreparedModelDraws& prepared,
    const PreviewScene& scene)
{
    nw::render::collect_prepared_model_surface_draws(
        surfaces,
        prepared.common,
        prepared.ranges,
        scene.model_instances);
    nw::render::sort_prepared_model_surface_draws_by_pass(
        std::span<nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()},
        surfaces.sort_cache);
}

} // namespace

void append_prepared_render_model_draws(
    nw::render::PreparedModelDrawList& out,
    nw::render::ModelInstanceHandle handle,
    const nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    const nw::render::ModelMaterialOverrideStore& material_overrides)
{
    append_render_model_draws_impl(out, handle, instance, model, material_overrides);
}

void collect_prepared_model_draws(
    PreviewPreparedModelDraws& out,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    out.clear();
    out.common.instance_offsets.reserve(handles.size() + 1u);
    append_instance_offset(out);
    append_prepared_model_draws(out, scene, handles);
    out.transform_cache.resize(out.common.draws.size());
    nw::render::collect_prepared_model_draw_ranges(out.ranges, out.common);
}

void collect_prepared_model_draws(
    PreviewPreparedModelDraws& out,
    const PreviewScene& scene)
{
    collect_prepared_model_draws(out, scene, scene.static_model_instance_handles);
}

void collect_prepared_model_surface_draws(
    PreviewPreparedModelDraws& prepared,
    nw::render::PreparedModelSurfaceDrawList& surfaces,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    collect_prepared_model_draws(prepared, scene, handles);
    collect_sorted_surface_draws(surfaces, prepared, scene);
}

void collect_prepared_model_surface_draws(
    PreviewPreparedModelDraws& prepared,
    nw::render::PreparedModelSurfaceDrawList& surfaces,
    const PreviewScene& scene)
{
    collect_prepared_model_draws(prepared, scene);
    collect_sorted_surface_draws(surfaces, prepared, scene);
}

namespace {

std::span<const nw::render::PreparedModelSurfaceDraw> prepared_surface_pass_span(
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    nw::render::RenderPassSelection pass)
{
    switch (pass) {
    case nw::render::RenderPassSelection::opaque_cutout:
        return nw::render::prepared_model_surface_draws_for_pass_indices(surfaces, 0u, 2u);
    case nw::render::RenderPassSelection::water:
        return nw::render::prepared_model_surface_draws_for_pass_indices(surfaces, 2u, 3u);
    case nw::render::RenderPassSelection::transparent:
        return nw::render::prepared_model_surface_draws_for_pass_indices(surfaces, 3u, 4u);
    case nw::render::RenderPassSelection::all:
        return surfaces;
    }
    return {};
}

bool prepared_surface_visible_in_pass(
    nw::render::MaterialMode mode,
    nw::render::RenderPassSelection pass) noexcept
{
    switch (pass) {
    case nw::render::RenderPassSelection::opaque_cutout:
        return mode == nw::render::MaterialMode::opaque
            || mode == nw::render::MaterialMode::cutout;
    case nw::render::RenderPassSelection::water:
        return mode == nw::render::MaterialMode::water;
    case nw::render::RenderPassSelection::transparent:
        return mode == nw::render::MaterialMode::transparent;
    case nw::render::RenderPassSelection::all:
        return true;
    }
    return false;
}

void prepare_surface_batches(
    PreparedSurfaceBatches& batches,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const uint32_t> surface_indices,
    nw::render::RenderPassSelection pass,
    const AreaRenderFrame* visibility_filter)
{
    batches = {};
    auto& valid_surface_indices = batches.surface_indices;
    auto& runs = batches.runs;
    auto& batch_groups = batches.groups;
    auto& fallback_runs = batches.fallback_runs;
    if (surface_indices.size()
        > std::numeric_limits<uint32_t>::max()) {
        batches.dropped_invalid_surface_count
            = std::numeric_limits<uint32_t>::max();
        return;
    }
    valid_surface_indices.reserve(surface_indices.size());
    for (const uint32_t surface_index : surface_indices) {
        if (surface_index >= surfaces.size()) {
            add_saturating(batches.dropped_invalid_surface_count, 1u);
            continue;
        }
        const auto& surface = surfaces[surface_index];
        if (visibility_filter
            && !visibility_filter->record_visible(
                surface.handle_index)) {
            continue;
        }
        if (!prepared_surface_visible_in_pass(
                surface.material_mode, pass)) {
            continue;
        }
        if (surface.instance_source_index
                == nw::render::kInvalidPreparedModelDrawIndex
            || surface.instance_source_index
                >= scene.static_models.size()) {
            add_saturating(batches.dropped_invalid_surface_count, 1u);
            continue;
        }
        valid_surface_indices.push_back(surface_index);
    }

    runs.reserve(valid_surface_indices.size());
    for (size_t cursor = 0u;
        cursor < valid_surface_indices.size();) {
        const size_t begin = cursor;
        const uint32_t source_index
            = surfaces[valid_surface_indices[cursor]]
                  .instance_source_index;
        ++cursor;
        while (cursor < valid_surface_indices.size()
            && surfaces[valid_surface_indices[cursor]]
                    .instance_source_index
                == source_index) {
            ++cursor;
        }
        runs.push_back({
            .begin = begin,
            .end = cursor,
            .instance_source_index = source_index,
        });
    }

    const auto surface_at = [&](const PreparedSurfaceBatches::Run& run,
                                size_t offset)
        -> const nw::render::PreparedModelSurfaceDraw& {
        return surfaces[valid_surface_indices[run.begin + offset]];
    };
    const auto batch_eligible = [&](const PreparedSurfaceBatches::Run& run,
                                    const nw::render::ModelInstance& instance) {
        if (run.begin >= run.end) {
            return false;
        }
        for (size_t offset = 0u;
            offset < run.end - run.begin; ++offset) {
            const auto& surface = surface_at(run, offset);
            if (surface.skinned
                || (surface.material_mode
                        != nw::render::MaterialMode::opaque
                    && surface.material_mode
                        != nw::render::MaterialMode::cutout)) {
                return false;
            }
            if (instance.scene_animation_enabled) {
                const auto& model = scene.static_models[run.instance_source_index];
                if (surface.source_draw_index >= model->primitives.size()
                    || model->primitives[surface.source_draw_index].deformer
                        != nw::render::kInvalidModelDeformerIndex) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto batch_compatible = [&](const PreparedSurfaceBatches::Run& lhs,
                                      const PreparedSurfaceBatches::Run& rhs) {
        const size_t left_count = lhs.end - lhs.begin;
        if (left_count != rhs.end - rhs.begin) {
            return false;
        }
        for (size_t offset = 0u; offset < left_count; ++offset) {
            const auto& left = surface_at(lhs, offset);
            const auto& right = surface_at(rhs, offset);
            if (left.source_draw_index != right.source_draw_index
                || left.material_index != right.material_index
                || left.material_override != right.material_override
                || left.skin_index != right.skin_index
                || left.material_mode != right.material_mode
                || left.material_uses_fallback
                    != right.material_uses_fallback
                || left.material_payload != right.material_payload
                || left.skinned != right.skinned
                || left.winding != right.winding) {
                return false;
            }
        }
        return true;
    };

    absl::flat_hash_map<const nw::render::RenderModel*, std::vector<uint32_t>> model_group_indices;
    batch_groups.reserve(runs.size());
    fallback_runs.reserve(runs.size());
    for (uint32_t run_index = 0u;
        run_index < runs.size(); ++run_index) {
        const auto& run = runs[run_index];
        const auto& model
            = scene.static_models[run.instance_source_index];
        const auto* instance = scene.static_model_instance(run.instance_source_index);
        if (!model || !instance || !batch_eligible(run, *instance)) {
            fallback_runs.push_back(run_index);
            continue;
        }
        const auto space = instance->scene_animation_enabled
            ? nw::render::PreparedModelSurfaceInstanceSpace::primitive_world
            : nw::render::PreparedModelSurfaceInstanceSpace::model_root;

        // Placements share asset identity; keep group indices in creation order.
        auto& group_indices = model_group_indices[model.get()];
        const auto group = std::find_if(group_indices.begin(),
            group_indices.end(), [&](uint32_t group_index) {
                const auto& candidate = batch_groups[group_index];
                return candidate.space == space
                    && batch_compatible(runs[candidate.representative_run], run);
            });
        if (group == group_indices.end()) {
            group_indices.push_back(static_cast<uint32_t>(batch_groups.size()));
            batch_groups.push_back({
                .representative_run = run_index,
                .space = space,
                .run_indices = {run_index},
            });
        } else {
            batch_groups[*group].run_indices.push_back(run_index);
        }
    }
}

void retain_root_instances(PreparedSurfaceBatches& batches, const PreviewScene& scene)
{
    // Preparation has validated each run's instance. Gather while those
    // inputs are still owned by this synchronous cache rebuild.
    const size_t max_rows = std::numeric_limits<uint32_t>::max()
        / sizeof(nw::render::PreparedModelSurfaceInstance);
    for (auto& group : batches.groups) {
        if (group.space != nw::render::PreparedModelSurfaceInstanceSpace::model_root
            || group.run_indices.size() < 2u
            || group.run_indices.size() > max_rows) {
            continue;
        }
        group.root_instances.reserve(group.run_indices.size());
        for (const uint32_t run_index : group.run_indices) {
            const auto* instance = scene.static_model_instance(
                batches.runs[run_index].instance_source_index);
            group.root_instances.push_back({
                .root = instance->root_transform,
                .root_normal_matrix = make_normal_matrix(instance->root_transform),
            });
        }
    }
}

nw::render::PreparedRenderModelSurfaceSubmissionStats render_surface_batches(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    const PreparedSurfaceBatches& batches,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const nw::render::PreparedRenderModelSkinTable* skin_table,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch)
{
    nw::render::PreparedRenderModelSurfaceSubmissionStats stats{};
    stats.dropped_invalid_surface_count = batches.dropped_invalid_surface_count;
    const auto& runs = batches.runs;
    const auto& batch_groups = batches.groups;
    // Frame allocation failures must not alter the retained preparation result.
    auto fallback_runs = batches.fallback_runs;
    const auto surface_at = [&](const PreparedSurfaceBatches::Run& run,
                                size_t offset)
        -> const nw::render::PreparedModelSurfaceDraw& {
        return surfaces[batches.surface_indices[run.begin + offset]];
    };
    for (const auto& group : batch_groups) {
        if (group.run_indices.size() < 2u) {
            fallback_runs.push_back(group.representative_run);
            continue;
        }
        const auto& representative
            = runs[group.representative_run];
        const auto& model = scene.static_models[representative.instance_source_index];
        const size_t surface_count
            = representative.end - representative.begin;
        const size_t instance_count = group.run_indices.size();
        const bool world_space = group.space
            == nw::render::PreparedModelSurfaceInstanceSpace::primitive_world;
        const size_t slice_count = world_space ? surface_count : 1u;
        const size_t max_rows = std::numeric_limits<uint32_t>::max()
            / sizeof(nw::render::PreparedModelSurfaceInstance);
        if (!render_model_ctx.gpu
            || instance_count > max_rows / slice_count) {
            fallback_runs.insert(fallback_runs.end(),
                group.run_indices.begin(), group.run_indices.end());
            continue;
        }
        const uint32_t instance_bytes
            = static_cast<uint32_t>(instance_count * slice_count)
            * sizeof(nw::render::PreparedModelSurfaceInstance);
        const auto mapped_instances
            = render_model_ctx.gpu->allocate_mapped_frame_storage(
                cmd, instance_bytes, 64u);
        if (!mapped_instances.span.buffer.valid()
            || !mapped_instances.data) {
            fallback_runs.insert(fallback_runs.end(),
                group.run_indices.begin(), group.run_indices.end());
            continue;
        }
        auto* surface_instances = static_cast<
            nw::render::PreparedModelSurfaceInstance*>(
            mapped_instances.data);
        if (world_space) {
            for (size_t surface_offset = 0; surface_offset < surface_count; ++surface_offset) {
                for (size_t instance_offset = 0; instance_offset < instance_count; ++instance_offset) {
                    const auto& surface = surface_at(runs[group.run_indices[instance_offset]], surface_offset);
                    surface_instances[surface_offset * instance_count + instance_offset] = {
                        .root = surface.world,
                        .root_normal_matrix = surface.normal_matrix,
                    };
                }
            }
        } else if (group.root_instances.size() == instance_count) {
            std::copy(group.root_instances.begin(), group.root_instances.end(), surface_instances);
        } else {
            for (size_t instance_offset = 0u;
                instance_offset < instance_count;
                ++instance_offset) {
                const uint32_t run_index
                    = group.run_indices[instance_offset];
                const auto* instance = scene.static_model_instance(
                    runs[run_index].instance_source_index);
                surface_instances[instance_offset] = {
                    .root = instance->root_transform,
                    .root_normal_matrix
                    = make_normal_matrix(instance->root_transform),
                };
            }
        }
        for (size_t surface_offset = 0u;
            surface_offset < surface_count;
            ++surface_offset) {
            nw::render::render_prepared_render_model_surface_instances(
                render_model_ctx, cmd, *model,
                surface_at(representative, surface_offset),
                mapped_instances.span,
                world_space ? static_cast<uint32_t>(surface_offset * instance_count) : 0u,
                static_cast<uint32_t>(instance_count),
                ctx, pass,
                &scene.material_overrides, &stats, group.space,
                surface_at(representative, surface_offset).winding);
        }
    }

    std::sort(fallback_runs.begin(), fallback_runs.end());
    std::vector<nw::render::PreparedModelSurfaceDraw>
        fallback_surfaces;
    for (const uint32_t run_index : fallback_runs) {
        const auto& run = runs[run_index];
        const auto& model
            = scene.static_models[run.instance_source_index];
        if (!model) {
            add_saturating(stats.dropped_invalid_surface_count,
                saturating_count(run.end - run.begin));
            continue;
        }
        fallback_surfaces.clear();
        fallback_surfaces.reserve(run.end - run.begin);
        for (size_t offset = 0u;
            offset < run.end - run.begin; ++offset) {
            fallback_surfaces.push_back(surface_at(run, offset));
        }
        nw::render::render_prepared_render_model_surfaces(
            render_model_ctx, cmd, *model,
            fallback_surfaces, ctx, pass, skin_table,
            &scene.material_overrides, &stats, packet_scratch);
    }

    return stats;
}

nw::render::PreparedRenderModelSurfaceSubmissionStats render_prepared_render_model_surface_draw_indices_for_pass(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const uint32_t> surface_indices,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const AreaRenderFrame* visibility_filter,
    const nw::render::PreparedRenderModelSkinTable* skin_table,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch)
{
    PreparedSurfaceBatches batches;
    prepare_surface_batches(batches, scene, surfaces, surface_indices, pass, visibility_filter);
    return render_surface_batches(render_model_ctx, cmd, scene, surfaces, batches,
        ctx, pass, skin_table, packet_scratch);
}

} // namespace

nw::render::PreparedRenderModelSurfaceSubmissionStats render_prepared_render_model_surface_draws(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const nw::render::PreparedRenderModelSkinTable* skin_table,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch)
{
    const auto pass_surfaces
        = prepared_surface_pass_span(surfaces, pass);
    if (pass_surfaces.size()
        > std::numeric_limits<uint32_t>::max()) {
        nw::render::PreparedRenderModelSurfaceSubmissionStats stats{};
        stats.dropped_invalid_surface_count
            = std::numeric_limits<uint32_t>::max();
        return stats;
    }
    std::vector<uint32_t> surface_indices(pass_surfaces.size());
    for (size_t index = 0u;
        index < surface_indices.size(); ++index) {
        surface_indices[index] = static_cast<uint32_t>(index);
    }
    return render_prepared_render_model_surface_draw_indices_for_pass(
        render_model_ctx, cmd, scene, pass_surfaces,
        surface_indices, ctx, pass, nullptr, skin_table,
        packet_scratch);
}

nw::render::PreparedRenderModelSurfaceSubmissionStats render_prepared_render_model_surface_draw_indices(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    std::span<const uint32_t> surface_indices,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const AreaRenderFrame* visibility_filter,
    const nw::render::PreparedRenderModelSkinTable* skin_table,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch)
{
    return render_prepared_render_model_surface_draw_indices_for_pass(
        render_model_ctx, cmd, scene, surfaces,
        surface_indices, ctx, pass, visibility_filter, skin_table,
        packet_scratch);
}

nw::render::PreparedRenderModelSurfaceSubmissionStats render_area_model_surface_draws(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    AreaRenderScene& area_scene,
    const AreaRenderFrame& frame,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch)
{
    const auto& surfaces = area_scene.prepared_model_surface_draws();
    std::span<const uint32_t> indices;
    switch (pass) {
    case nw::render::RenderPassSelection::opaque_cutout:
        indices = frame.visible_opaque_cutout_prepared_surface_indices();
        break;
    case nw::render::RenderPassSelection::water:
        indices = frame.visible_water_prepared_surface_indices();
        break;
    case nw::render::RenderPassSelection::transparent:
        indices = frame.visible_transparent_prepared_surface_indices();
        break;
    case nw::render::RenderPassSelection::all:
        indices = frame.visible_prepared_surface_indices();
        break;
    }
    if (pass == nw::render::RenderPassSelection::opaque_cutout
        && frame.uses_cached_draw_lists()
        && !frame.filters_cached_draw_records()) {
        auto& cache = area_scene.surface_batch_cache();
        if (!cache.valid
            || cache.static_generation != area_scene.static_cache_generation()
            || cache.tile_generation != area_scene.tile_cache_generation()) {
            prepare_surface_batches(cache.batches, scene, surfaces.draws, indices, pass, nullptr);
            retain_root_instances(cache.batches, scene);
            cache.static_generation = area_scene.static_cache_generation();
            cache.tile_generation = area_scene.tile_cache_generation();
            cache.valid = true;
            ++cache.rebuild_count;
        }
        return render_surface_batches(render_model_ctx, cmd, scene, surfaces.draws,
            cache.batches, ctx, pass, &surfaces.render_model_skins, packet_scratch);
    }
    return render_prepared_render_model_surface_draw_indices_for_pass(
        render_model_ctx, cmd, scene, surfaces.draws, indices, ctx, pass,
        frame.filters_cached_draw_records() ? &frame : nullptr,
        &surfaces.render_model_skins, packet_scratch);
}

PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    PreviewPreparedModelDrawValidation validation{};
    validation.prepared_stats = prepared.common.stats;
    validation.prepared_draw_count = saturating_count(prepared.common.draws.size());
    validation.instance_offset_count = saturating_count(prepared.common.instance_offsets.size());

    append_expected_model_draws(validation, scene, handles);
    compare_prepared_stats(validation);
    validate_instance_offsets(validation, prepared);
    validate_prepared_draw_records(validation, scene, prepared);
    if (!material_stats_equal(validation.expected_materials, validation.prepared_materials)) {
        add_protocol_mismatch(validation);
    }
    return validation;
}

PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared)
{
    PreviewPreparedModelDrawValidation validation{};
    validation.prepared_stats = prepared.common.stats;
    validation.prepared_draw_count = saturating_count(prepared.common.draws.size());
    validation.instance_offset_count = saturating_count(prepared.common.instance_offsets.size());

    append_expected_model_draws(validation, scene, scene.static_model_instance_handles);
    compare_prepared_stats(validation);
    validate_instance_offsets(validation, prepared);
    validate_prepared_draw_records(validation, scene, prepared);
    if (!material_stats_equal(validation.expected_materials, validation.prepared_materials)) {
        add_protocol_mismatch(validation);
    }
    return validation;
}

} // namespace nw::render::viewer
