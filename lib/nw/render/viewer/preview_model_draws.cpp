#include "preview_model_draws.hpp"

#include "preview_model_animation.hpp"
#include "preview_scene.hpp"

#include <nw/render/model_renderer.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/render_service.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
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
    compare_stat(expected.nwn_legacy_instance_count, prepared.nwn_legacy_instance_count, validation);
    compare_stat(expected.render_model_draw_count, prepared.render_model_draw_count, validation);
    compare_stat(expected.nwn_legacy_draw_count, prepared.nwn_legacy_draw_count, validation);
    compare_stat(expected.material_fallback_draw_count, prepared.material_fallback_draw_count, validation);
    compare_stat(
        expected.render_model_material_fallback_draw_count,
        prepared.render_model_material_fallback_draw_count,
        validation);
    compare_stat(
        expected.nwn_legacy_material_fallback_draw_count,
        prepared.nwn_legacy_material_fallback_draw_count,
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

void append_render_model_draws(
    PreviewPreparedModelDraws& out,
    const nw::render::ModelInstanceHandle handle,
    const nw::render::ModelInstance& instance,
    const nw::render::RenderModel& model,
    const nw::render::ModelMaterialOverrideStore& material_overrides)
{
    auto& stats = out.common.stats;
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
        out.common.draws.push_back(nw::render::PreparedModelDraw{
            .instance = handle,
            .instance_kind = instance.kind,
            .kind = nw::render::PreparedModelDrawKind::render_model,
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
            .normal_matrix = make_normal_matrix(world),
            .bounds = transform_bounds(primitive.bounds, world),
        });
        ++stats.render_model_draw_count;
        if (draw_material.material_uses_fallback) {
            add_saturating(stats.material_fallback_draw_count, 1u);
            add_saturating(stats.render_model_material_fallback_draw_count, 1u);
        }
    }
}

void append_nwn_legacy_draws(
    PreviewPreparedModelDraws& out,
    const nw::render::ModelInstanceHandle handle,
    const nw::render::ModelInstance& instance,
    const nw::render::nwn::ModelInstance& model)
{
    auto& stats = out.common.stats;
    ++stats.nwn_legacy_instance_count;
    const size_t begin = out.nwn_draws.size();
    nw::render::nwn::collect_prepared_draws(out.nwn_draws, model, instance.root_transform);
    const auto& shadow_casters = model.shadow_casters();
    for (size_t draw_index = begin; draw_index < out.nwn_draws.size(); ++draw_index) {
        const auto& sidecar_draw = out.nwn_draws[draw_index];
        const auto* mesh = sidecar_draw.mesh;
        if (!mesh) {
            ++stats.invalid_draw_count;
            continue;
        }
        out.common.draws.push_back(nw::render::PreparedModelDraw{
            .instance = handle,
            .instance_kind = instance.kind,
            .kind = nw::render::PreparedModelDrawKind::nwn_legacy,
            .instance_source_index = instance.nwn_legacy_model_index,
            .source_draw_index = saturating_count(draw_index),
            .material_override = {},
            .material_mode = mesh->material_mode,
            .material_uses_fallback = mesh->material_uses_fallback,
            .material_payload = nw::render::prepared_nwn_legacy_material_payload_kind(
                mesh->uses_plt,
                mesh->material_uses_fallback),
            .skinned = mesh->is_skin,
            .instance_casts_shadow = instance.shadow.casts_shadow,
            .primitive_casts_shadow = std::find(shadow_casters.begin(), shadow_casters.end(), mesh) != shadow_casters.end(),
            .world = sidecar_draw.world,
            .normal_matrix = sidecar_draw.normal_matrix,
            .bounds = sidecar_draw.light_bounds,
        });
        ++stats.nwn_legacy_draw_count;
        if (mesh->material_uses_fallback) {
            add_saturating(stats.material_fallback_draw_count, 1u);
            add_saturating(stats.nwn_legacy_material_fallback_draw_count, 1u);
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
        switch (instance->kind) {
        case nw::render::ModelInstanceKind::render_model:
            if (instance->render_model_index >= scene.static_models.size()
                || !scene.static_models[instance->render_model_index]) {
                ++out.common.stats.missing_asset_count;
                break;
            }
            append_render_model_draws(
                out,
                handle,
                *instance,
                *scene.static_models[instance->render_model_index],
                scene.material_overrides);
            break;
        case nw::render::ModelInstanceKind::nwn_legacy:
            if (instance->nwn_legacy_model_index >= scene.models.size()
                || !scene.models[instance->nwn_legacy_model_index]) {
                ++out.common.stats.missing_asset_count;
                break;
            }
            append_nwn_legacy_draws(
                out,
                handle,
                *instance,
                *scene.models[instance->nwn_legacy_model_index]);
            break;
        }
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

void append_expected_nwn_legacy_draws(
    PreviewPreparedModelDrawValidation& validation,
    const nw::render::ModelInstance& instance,
    const nw::render::nwn::ModelInstance& model,
    std::vector<nw::render::nwn::PreparedDrawItem>& scratch)
{
    auto& stats = validation.expected_stats;
    ++stats.nwn_legacy_instance_count;
    const size_t begin = scratch.size();
    nw::render::nwn::collect_prepared_draws(scratch, model, instance.root_transform);
    for (size_t draw_index = begin; draw_index < scratch.size(); ++draw_index) {
        const auto& draw = scratch[draw_index];
        if (!draw.mesh) {
            ++stats.invalid_draw_count;
            continue;
        }

        ++stats.nwn_legacy_draw_count;
        if (draw.mesh->material_uses_fallback) {
            add_saturating(stats.material_fallback_draw_count, 1u);
            add_saturating(stats.nwn_legacy_material_fallback_draw_count, 1u);
        }
        add_material_stat(validation.expected_materials, draw.mesh->material_mode);
    }
}

void append_expected_model_draws(
    PreviewPreparedModelDrawValidation& validation,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    std::vector<nw::render::nwn::PreparedDrawItem> nwn_scratch;
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
        switch (instance->kind) {
        case nw::render::ModelInstanceKind::render_model:
            if (instance->render_model_index >= scene.static_models.size()
                || !scene.static_models[instance->render_model_index]) {
                ++validation.expected_stats.missing_asset_count;
                break;
            }
            append_expected_render_model_draws(
                validation,
                *instance,
                *scene.static_models[instance->render_model_index],
                scene.material_overrides);
            break;
        case nw::render::ModelInstanceKind::nwn_legacy:
            if (instance->nwn_legacy_model_index >= scene.models.size()
                || !scene.models[instance->nwn_legacy_model_index]) {
                ++validation.expected_stats.missing_asset_count;
                break;
            }
            append_expected_nwn_legacy_draws(
                validation,
                *instance,
                *scene.models[instance->nwn_legacy_model_index],
                nwn_scratch);
            break;
        }
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
    if (draw.instance_kind != nw::render::ModelInstanceKind::render_model
        || instance.kind != nw::render::ModelInstanceKind::render_model
        || draw.instance_source_index != instance.render_model_index
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

void validate_nwn_legacy_draw(
    PreviewPreparedModelDrawValidation& validation,
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared,
    const nw::render::PreparedModelDraw& draw,
    const nw::render::ModelInstance& instance)
{
    if (draw.instance_kind != nw::render::ModelInstanceKind::nwn_legacy
        || instance.kind != nw::render::ModelInstanceKind::nwn_legacy
        || draw.instance_source_index != instance.nwn_legacy_model_index
        || instance.nwn_legacy_model_index >= scene.models.size()
        || !scene.models[instance.nwn_legacy_model_index]) {
        add_protocol_mismatch(validation);
        return;
    }
    if (draw.source_draw_index >= prepared.nwn_draws.size()) {
        add_protocol_mismatch(validation);
        return;
    }

    const auto& sidecar_draw = prepared.nwn_draws[draw.source_draw_index];
    const auto& model = *scene.models[instance.nwn_legacy_model_index];
    const auto expected_payload = sidecar_draw.mesh
        ? nw::render::prepared_nwn_legacy_material_payload_kind(
              sidecar_draw.mesh->uses_plt,
              sidecar_draw.mesh->material_uses_fallback)
        : nw::render::PreparedModelMaterialPayloadKind::fallback;
    const bool expected_casts_shadow = sidecar_draw.mesh
        && std::find(model.shadow_casters().begin(), model.shadow_casters().end(), sidecar_draw.mesh)
            != model.shadow_casters().end();
    if (!sidecar_draw.mesh || draw.material_mode != sidecar_draw.mesh->material_mode
        || draw.material_uses_fallback != sidecar_draw.mesh->material_uses_fallback
        || draw.material_payload != expected_payload
        || draw.skinned != sidecar_draw.mesh->is_skin
        || draw.primitive_casts_shadow != expected_casts_shadow) {
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
        switch (draw.kind) {
        case nw::render::PreparedModelDrawKind::render_model:
            ++draw_stats.render_model_draw_count;
            validate_render_model_draw(validation, scene, draw, *instance);
            break;
        case nw::render::PreparedModelDrawKind::nwn_legacy:
            ++draw_stats.nwn_legacy_draw_count;
            validate_nwn_legacy_draw(validation, scene, prepared, draw, *instance);
            break;
        default:
            add_protocol_mismatch(validation);
            break;
        }
    }

    compare_stat(draw_stats.render_model_draw_count, validation.prepared_stats.render_model_draw_count, validation);
    compare_stat(draw_stats.nwn_legacy_draw_count, validation.prepared_stats.nwn_legacy_draw_count, validation);
    uint32_t stats_draw_count = 0;
    add_saturating(stats_draw_count, validation.prepared_stats.render_model_draw_count);
    add_saturating(stats_draw_count, validation.prepared_stats.nwn_legacy_draw_count);
    compare_stat(stats_draw_count, validation.prepared_draw_count, validation);
}

bool nwn_legacy_surface_matches_draw(
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
        std::span<nw::render::PreparedModelSurfaceDraw>{surfaces.draws.data(), surfaces.draws.size()});
}

} // namespace

void collect_prepared_model_draws(
    PreviewPreparedModelDraws& out,
    const PreviewScene& scene,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    out.clear();
    out.common.instance_offsets.reserve(handles.size() + 1u);
    append_instance_offset(out);
    append_prepared_model_draws(out, scene, handles);
    nw::render::collect_prepared_model_draw_ranges(out.ranges, out.common);
}

void collect_prepared_model_draws(
    PreviewPreparedModelDraws& out,
    const PreviewScene& scene)
{
    out.clear();
    const size_t handle_count = scene.model_instance_handles.size() + scene.static_model_instance_handles.size();
    out.common.instance_offsets.reserve(handle_count + 1u);
    append_instance_offset(out);
    append_prepared_model_draws(out, scene, scene.model_instance_handles);
    append_prepared_model_draws(out, scene, scene.static_model_instance_handles);
    nw::render::collect_prepared_model_draw_ranges(out.ranges, out.common);
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

PreviewPreparedNwnLegacyDrawItemStats collect_nwn_legacy_prepared_draw_items(
    std::vector<const nw::render::nwn::PreparedDrawItem*>& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelDraw> draws)
{
    PreviewPreparedNwnLegacyDrawItemStats stats{};
    stats.input_draw_count = saturating_count(draws.size());

    out.clear();
    out.reserve(draws.size());
    for (const auto& draw : draws) {
        if (draw.kind != nw::render::PreparedModelDrawKind::nwn_legacy
            || draw.instance_kind != nw::render::ModelInstanceKind::nwn_legacy) {
            add_saturating(stats.non_nwn_draw_count, 1u);
            continue;
        }
        if (draw.source_draw_index >= prepared.nwn_draws.size()) {
            add_saturating(stats.missing_sidecar_draw_count, 1u);
            continue;
        }

        const auto& sidecar_draw = prepared.nwn_draws[draw.source_draw_index];
        const auto* mesh = sidecar_draw.mesh;
        const auto expected_payload = mesh
            ? nw::render::prepared_nwn_legacy_material_payload_kind(mesh->uses_plt, mesh->material_uses_fallback)
            : nw::render::PreparedModelMaterialPayloadKind::fallback;
        if (!mesh || mesh->material_mode != draw.material_mode
            || mesh->material_uses_fallback != draw.material_uses_fallback
            || draw.material_payload != expected_payload
            || mesh->is_skin != draw.skinned) {
            add_saturating(stats.invalid_sidecar_draw_count, 1u);
            continue;
        }

        out.push_back(&sidecar_draw);
        add_saturating(stats.selected_draw_count, 1u);
    }
    return stats;
}

void collect_nwn_legacy_prepared_surface_packets(
    PreviewPreparedNwnLegacySurfacePacketList& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces)
{
    out.clear();
    out.stats.input_draw_count = saturating_count(surfaces.size());
    out.surface_indices.reserve(surfaces.size());

    for (size_t surface_index = 0; surface_index < surfaces.size(); ++surface_index) {
        const auto& surface = surfaces[surface_index];
        if (surface.payload_kind != nw::render::PreparedModelDrawKind::nwn_legacy
            || surface.instance_kind != nw::render::ModelInstanceKind::nwn_legacy) {
            add_saturating(out.stats.non_nwn_draw_count, 1u);
            continue;
        }
        if (surface.draw_index >= prepared.common.draws.size()
            || surface.source_draw_index >= prepared.nwn_draws.size()) {
            add_saturating(out.stats.missing_sidecar_draw_count, 1u);
            continue;
        }

        const auto& draw = prepared.common.draws[surface.draw_index];
        const auto& sidecar_draw = prepared.nwn_draws[surface.source_draw_index];
        const auto* mesh = sidecar_draw.mesh;
        const auto expected_payload = mesh
            ? nw::render::prepared_nwn_legacy_material_payload_kind(mesh->uses_plt, mesh->material_uses_fallback)
            : nw::render::PreparedModelMaterialPayloadKind::fallback;
        if (!nwn_legacy_surface_matches_draw(surface, draw)
            || !mesh
            || mesh->material_mode != surface.material_mode
            || mesh->material_uses_fallback != surface.material_uses_fallback
            || surface.material_payload != expected_payload
            || mesh->is_skin != surface.skinned) {
            add_saturating(out.stats.invalid_sidecar_draw_count, 1u);
            continue;
        }

        out.surface_indices.push_back(saturating_count(surface_index));
        add_saturating(out.stats.selected_draw_count, 1u);
    }
}

PreviewPreparedNwnLegacyDrawItemStats collect_nwn_legacy_prepared_surface_draw_items(
    std::vector<const nw::render::nwn::PreparedDrawItem*>& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces)
{
    PreviewPreparedNwnLegacySurfacePacketList packets;
    collect_nwn_legacy_prepared_surface_packets(packets, prepared, surfaces);

    out.clear();
    out.reserve(packets.surface_indices.size());
    for (const uint32_t surface_index : packets.surface_indices) {
        if (surface_index >= surfaces.size()) {
            continue;
        }
        out.push_back(&prepared.nwn_draws[surfaces[surface_index].source_draw_index]);
    }
    return packets.stats;
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

nw::render::PreparedRenderModelSurfaceSubmissionStats render_prepared_render_model_surface_draws_for_pass(
    const nw::render::ModelRenderContext& render_model_ctx,
    nw::gfx::CommandList* cmd,
    const PreviewScene& scene,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    const nw::render::PreparedRenderModelSkinTable* skin_table,
    nw::render::PreparedRenderModelSurfacePacketList* packet_scratch)
{
    nw::render::PreparedRenderModelSurfaceSubmissionStats stats{};

    nw::render::PreparedRenderModelSurfaceRunList runs;
    nw::render::collect_prepared_render_model_surface_runs(
        runs,
        surfaces,
        scene.static_models.size());
    add_saturating(
        stats.dropped_invalid_surface_count,
        runs.stats.invalid_source_index_count);

    for (const auto& run : runs.runs) {
        const auto& model = scene.static_models[run.instance_source_index];
        if (!model) {
            add_saturating(
                stats.dropped_invalid_surface_count,
                saturating_count(run.draws.size()));
            continue;
        }
        nw::render::render_prepared_render_model_surfaces(
            render_model_ctx,
            cmd,
            *model,
            run.draws,
            ctx,
            pass,
            skin_table,
            &scene.material_overrides,
            &stats,
            packet_scratch);
    }

    return stats;
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
    return render_prepared_render_model_surface_draws_for_pass(
        render_model_ctx,
        cmd,
        scene,
        prepared_surface_pass_span(surfaces, pass),
        ctx,
        pass,
        skin_table,
        packet_scratch);
}

PreviewPreparedNwnLegacyDrawItemStats collect_nwn_legacy_prepared_surface_draw_items(
    std::vector<const nw::render::nwn::PreparedDrawItem*>& out,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    nw::render::RenderPassSelection pass)
{
    return collect_nwn_legacy_prepared_surface_draw_items(
        out,
        prepared,
        prepared_surface_pass_span(surfaces, pass));
}

PreviewPreparedNwnLegacyDrawItemStats render_prepared_nwn_legacy_surface_draws(
    nw::render::RenderService& render_service,
    nw::gfx::CommandList* cmd,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::PreparedModelSurfaceDraw> surfaces,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass,
    nw::render::nwn::PreparedDrawScratch& scratch,
    std::vector<const nw::render::nwn::PreparedDrawItem*>& nwn_draw_items)
{
    const auto pass_surfaces = prepared_surface_pass_span(surfaces, pass);
    const auto stats = collect_nwn_legacy_prepared_surface_draw_items(
        nwn_draw_items,
        prepared,
        pass_surfaces);
    if (!cmd || nwn_draw_items.empty()) {
        return stats;
    }

    auto nwn_render_ctx = render_service.nwn_model_render_context();
    nw::render::nwn::render_prepared_model_draws(
        nwn_render_ctx,
        cmd,
        std::span<const nw::render::nwn::PreparedDrawItem* const>{
            nwn_draw_items.data(),
            nwn_draw_items.size()},
        ctx,
        scratch,
        pass);

    return stats;
}

PreviewPreparedModelDrawValidation validate_prepared_model_draws(
    const PreviewScene& scene,
    const PreviewPreparedModelDraws& prepared,
    std::span<const nw::render::ModelInstanceHandle> handles)
{
    PreviewPreparedModelDrawValidation validation{};
    validation.prepared_stats = prepared.common.stats;
    validation.prepared_draw_count = saturating_count(prepared.common.draws.size());
    validation.nwn_sidecar_draw_count = saturating_count(prepared.nwn_draws.size());
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
    validation.nwn_sidecar_draw_count = saturating_count(prepared.nwn_draws.size());
    validation.instance_offset_count = saturating_count(prepared.common.instance_offsets.size());

    append_expected_model_draws(validation, scene, scene.model_instance_handles);
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
