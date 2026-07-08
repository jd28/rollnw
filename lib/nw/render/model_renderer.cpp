#include "model_renderer.hpp"

#include <nw/render/model_gpu_backend.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace nw::render {

namespace {

enum class RenderModelSkinMatrixSource : uint8_t {
    none,
    source_matrices,
    table_matrices,
    bind_pose_fallback,
    missing_table,
    invalid_table_index,
    invalid_table_range,
};

struct RenderModelSkinMatrices {
    std::span<const glm::mat4> matrices{};
    RenderModelSkinMatrixSource source = RenderModelSkinMatrixSource::none;
    bool available = false;
};

void add_saturating(uint32_t& target, size_t value) noexcept
{
    const uint64_t total = static_cast<uint64_t>(target) + static_cast<uint64_t>(value);
    target = total > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(total);
}

void add_saturating(uint32_t& target, uint32_t value) noexcept
{
    add_saturating(target, static_cast<size_t>(value));
}

bool is_translucent_material(MaterialMode mode) noexcept
{
    return mode == MaterialMode::transparent || mode == MaterialMode::water;
}

RenderModelSkinMatrices source_skin_matrices_for_primitive(
    const ModelInstance* instance,
    uint32_t skin_index) noexcept
{
    if (!instance || skin_index >= instance->animation.skin_matrices.size()) {
        return {};
    }

    const auto& matrices = instance->animation.skin_matrices[skin_index];
    return RenderModelSkinMatrices{
        .matrices = std::span<const glm::mat4>{matrices.data(), matrices.size()},
        .source = RenderModelSkinMatrixSource::source_matrices,
        .available = true,
    };
}

RenderModelSkinMatrices prepared_surface_skin_matrices(
    const PreparedRenderModelSkinTable* skin_table,
    const PreparedModelSurfaceDraw& surface) noexcept
{
    if (!surface.skinned) {
        return {};
    }
    if (!skin_table) {
        return RenderModelSkinMatrices{.source = RenderModelSkinMatrixSource::missing_table};
    }
    if (surface.skin_table_index == kInvalidPreparedModelDrawIndex) {
        return RenderModelSkinMatrices{.source = RenderModelSkinMatrixSource::bind_pose_fallback};
    }
    if (surface.skin_table_index >= skin_table->ranges.size()) {
        return RenderModelSkinMatrices{.source = RenderModelSkinMatrixSource::invalid_table_index};
    }

    const auto& range = skin_table->ranges[surface.skin_table_index];
    if (range.source_skin_index != surface.skin_index
        || range.matrix_begin > skin_table->matrices.size()
        || range.matrix_count > skin_table->matrices.size() - range.matrix_begin) {
        return RenderModelSkinMatrices{.source = RenderModelSkinMatrixSource::invalid_table_range};
    }

    const auto* matrices = range.matrix_count == 0
        ? nullptr
        : skin_table->matrices.data() + range.matrix_begin;
    return RenderModelSkinMatrices{
        .matrices = std::span<const glm::mat4>{matrices, range.matrix_count},
        .source = RenderModelSkinMatrixSource::table_matrices,
        .available = true,
    };
}

const Material* prepared_surface_material(
    const RenderModel& model,
    const Primitive& primitive,
    const PreparedModelSurfaceDraw& surface,
    const ModelMaterialOverrideStore* material_overrides) noexcept
{
    if (surface.material_override.valid()) {
        if (!material_overrides) {
            return nullptr;
        }
        const auto* override = material_overrides->get(surface.material_override);
        return override ? &override->material : nullptr;
    }
    return primitive.material < model.materials.size() ? &model.materials[primitive.material] : nullptr;
}

bool prepared_shadow_surface_matches_model(
    const RenderModel& model,
    const PreparedModelSurfaceDraw& surface,
    const ModelMaterialOverrideStore* material_overrides) noexcept
{
    if (!surface.casts_shadow
        || !prepared_model_surface_is_render_model(surface)
        || surface.instance_source_index == kInvalidPreparedModelDrawIndex
        || surface.source_draw_index >= model.primitives.size()) {
        return false;
    }

    const auto& primitive = model.primitives[surface.source_draw_index];
    if (primitive.material >= model.materials.size()
        || surface.material_index != primitive.material
        || surface.skin_index != primitive.skin
        || surface.skinned != primitive.skinned) {
        return false;
    }

    const auto* material = prepared_surface_material(model, primitive, surface, material_overrides);
    return material
        && primitive.casts_shadow
        && prepared_model_surface_casts_shadow(surface.material_mode)
        && surface.material_mode == material->alpha_mode
        && surface.material_uses_fallback == material->material_uses_fallback
        && surface.material_payload == prepared_render_model_material_payload_kind(*material);
}

void add_prepared_render_model_skin_binding_stats(
    PreparedRenderModelSkinBindingStats& stats,
    RenderModelSkinMatrixSource source) noexcept
{
    switch (source) {
    case RenderModelSkinMatrixSource::none:
    case RenderModelSkinMatrixSource::source_matrices:
        break;
    case RenderModelSkinMatrixSource::table_matrices:
        add_saturating(stats.skinned_surface_count, 1u);
        add_saturating(stats.table_bound_surface_count, 1u);
        break;
    case RenderModelSkinMatrixSource::bind_pose_fallback:
        add_saturating(stats.skinned_surface_count, 1u);
        add_saturating(stats.bind_pose_fallback_surface_count, 1u);
        break;
    case RenderModelSkinMatrixSource::missing_table:
        add_saturating(stats.skinned_surface_count, 1u);
        add_saturating(stats.missing_table_surface_count, 1u);
        break;
    case RenderModelSkinMatrixSource::invalid_table_index:
        add_saturating(stats.skinned_surface_count, 1u);
        add_saturating(stats.invalid_table_index_surface_count, 1u);
        break;
    case RenderModelSkinMatrixSource::invalid_table_range:
        add_saturating(stats.skinned_surface_count, 1u);
        add_saturating(stats.invalid_table_range_surface_count, 1u);
        break;
    }
}

void apply_render_model_material_plt_constants(SurfaceConstants& surf, const Material& material) noexcept
{
    surf.plt_enabled = material.plt_enabled ? 1u : 0u;
    surf.plt_colors0 = material.plt_colors0;
    surf.plt_colors1 = material.plt_colors1;
    surf.plt_colors2 = material.plt_colors2;
}

nw::gfx::StorageSpan plt_palette_storage(const ModelRenderContext& render_ctx,
    nw::gfx::StorageSpan fallback) noexcept
{
    if (!render_ctx.gpu || !render_ctx.gpu->plt_palette_buffer().valid()) {
        return fallback;
    }
    return nw::gfx::StorageSpan{render_ctx.gpu->plt_palette_buffer()};
}

void add_prepared_render_model_skin_binding_stats(
    PreparedRenderModelSkinBindingStats& target,
    const PreparedRenderModelSkinBindingStats& source) noexcept
{
    add_saturating(target.skinned_surface_count, source.skinned_surface_count);
    add_saturating(target.table_bound_surface_count, source.table_bound_surface_count);
    add_saturating(target.bind_pose_fallback_surface_count, source.bind_pose_fallback_surface_count);
    add_saturating(target.missing_table_surface_count, source.missing_table_surface_count);
    add_saturating(target.invalid_table_index_surface_count, source.invalid_table_index_surface_count);
    add_saturating(target.invalid_table_range_surface_count, source.invalid_table_range_surface_count);
}

glm::mat4 make_normal_matrix(const glm::mat4& model_matrix)
{
    return glm::mat4(glm::mat3(glm::transpose(glm::inverse(model_matrix))));
}

nw::gfx::Handle<nw::gfx::Pipeline> model_pipeline_for(
    const ModelRenderContext& render_ctx,
    ModelPipelineMeshKind mesh_kind,
    MaterialMode material,
    bool offscreen_pass = false,
    ModelPipelinePass pass = ModelPipelinePass::color)
{
    if (!render_ctx.gpu) {
        return {};
    }
    return render_ctx.gpu->pipeline({
        .mesh = mesh_kind,
        .material = material,
        .target = offscreen_pass ? ModelPipelineTarget::offscreen : ModelPipelineTarget::onscreen,
        .pass = pass,
    });
}

bool render_model_material_visible_in_pass(MaterialMode mode, RenderPassSelection pass) noexcept
{
    const bool is_transparent = is_translucent_material(mode);
    return !((pass == RenderPassSelection::opaque_cutout && is_transparent)
        || (pass == RenderPassSelection::water)
        || (pass == RenderPassSelection::transparent && !is_transparent));
}

bool render_model_skin_supported(const Skin& skin) noexcept
{
    return model_skin_bone_count_supported(skin.joints.size());
}

bool fill_render_model_bones(
    std::array<glm::mat4, kModelMaxSkinBones>& bones,
    const RenderModel& model,
    const Primitive& prim,
    const RenderModelSkinMatrices& skin_matrices,
    uint32_t& bone_count)
{
    if (prim.skin >= model.skins.size() || prim.node < 0
        || static_cast<size_t>(prim.node) >= model.nodes.size()) {
        return false;
    }

    const auto& skin = model.skins[prim.skin];
    if (!render_model_skin_supported(skin)) {
        return false;
    }

    for (auto& bone : bones) {
        bone = glm::mat4(1.0f);
    }
    const glm::mat4 inverse_mesh = glm::inverse(model.nodes[prim.node].world_transform);
    if (skin_matrices.available) {
        const auto src = skin_matrices.matrices;
        for (size_t i = 0; i < src.size() && i < bones.size(); ++i) {
            bones[i] = inverse_mesh * src[i];
        }
        bone_count = static_cast<uint32_t>(std::min<size_t>(skin_matrices.matrices.size(), bones.size()));
        return true;
    }

    for (size_t i = 0; i < skin.joints.size(); ++i) {
        const int32_t joint = skin.joints[i];
        if (joint < 0 || static_cast<size_t>(joint) >= model.nodes.size()
            || i >= skin.inverse_bind_matrices.size()) {
            continue;
        }
        bones[i] = inverse_mesh * model.nodes[joint].world_transform * skin.inverse_bind_matrices[i];
    }
    bone_count = static_cast<uint32_t>(skin.joints.size());
    return true;
}

Lighting render_model_lighting(const RenderContext& ctx)
{
    Lighting lighting = ctx.lighting;
    if (glm::dot(lighting.ambient, lighting.ambient) < 1.0e-6f
        && lighting.key_intensity < 1.0e-4f
        && lighting.fill_intensity < 1.0e-4f
        && lighting.rim_intensity < 1.0e-4f) {
        lighting.key_direction = glm::normalize(glm::vec3{0.45f, -0.6f, 0.68f});
        lighting.key_color = glm::vec3{1.0f, 0.97f, 0.92f};
        lighting.key_intensity = 2.35f;
        lighting.fill_direction = glm::normalize(glm::vec3{-0.55f, -0.1f, 0.42f});
        lighting.fill_color = glm::vec3{0.42f, 0.48f, 0.58f};
        lighting.fill_intensity = 0.14f;
        lighting.rim_direction = glm::normalize(glm::vec3{0.0f, 0.92f, 0.35f});
        lighting.rim_color = glm::vec3{0.72f, 0.76f, 0.84f};
        lighting.rim_intensity = 0.06f;
        lighting.ambient = glm::vec3{0.008f, 0.010f, 0.012f};
    }
    return lighting;
}

bool render_render_model_primitive(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model,
    const Primitive& prim,
    const Material& material,
    const RenderContext& ctx,
    const SceneConstants& base_sc,
    nw::gfx::StorageSpan dummy_storage,
    const ForwardPlusStorageBindings& forward_plus_bindings,
    const glm::mat4& world,
    const glm::mat4& normal_matrix,
    const RenderModelSkinMatrices& skin_matrices)
{
    const auto mesh_kind = prim.skinned ? ModelPipelineMeshKind::pbr_skinned : ModelPipelineMeshKind::pbr_static;
    auto pipeline = model_pipeline_for(render_ctx, mesh_kind, material.alpha_mode);
    uint32_t vertex_stride = sizeof(Vertex);
    nw::gfx::StorageSpan bone_span{};
    if (prim.skinned) {
        vertex_stride = sizeof(SkinnedVertex);
        if (!pipeline.valid() || prim.skin >= model.skins.size() || prim.node < 0
            || static_cast<size_t>(prim.node) >= model.nodes.size()) {
            return false;
        }

        std::array<glm::mat4, kModelMaxSkinBones> bones{};
        uint32_t bone_count = 0;
        if (!fill_render_model_bones(bones, model, prim, skin_matrices, bone_count)) {
            return false;
        }
        bone_span = render_ctx.gpu->upload_bones(cmd, bones.data(), bone_count);
        if (!bone_span.buffer.valid()) {
            return false;
        }
    }
    if (!pipeline.valid()) {
        return false;
    }

    SceneConstants sc = base_sc;
    sc.model = world;
    sc.normal_matrix = normal_matrix;

    SurfaceConstants surf{};
    surf.albedo = material.albedo;
    surf.roughness = material.roughness;
    surf.metallic = material.metallic;
    surf.specular_strength = material.specular_strength;
    surf.normal_scale = material.normal_scale;
    surf.occlusion_strength = material.occlusion_strength;
    surf.ibl_strength = ctx.static_pbr_ibl_strength * material.ibl_strength;
    surf.exposure = ctx.static_pbr_exposure * material.exposure;
    surf.emissive = glm::vec4(material.emissive, 0.0f);
    surf.albedo_index = material.albedo_index;
    surf.normal_index = material.normal_index;
    surf.surface_index = material.surface_index;
    surf.emissive_index = material.emissive_index;
    surf.alpha_mode = static_cast<uint32_t>(material.alpha_mode);
    surf.alpha_cutoff = material.alpha_cutoff;
    surf.double_sided = material.double_sided ? 1u : 0u;
    surf.color_key_threshold = material.color_key_threshold;
    apply_render_model_material_plt_constants(surf, material);

    auto scene_uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(SceneConstants));
    auto surface_uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(SurfaceConstants));
    if (!scene_uniforms.data || !surface_uniforms.data) {
        return false;
    }
    std::memcpy(scene_uniforms.data, &sc, sizeof(sc));
    std::memcpy(surface_uniforms.data, &surf, sizeof(surf));

    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_vertex_buffer(cmd, prim.vertices, vertex_stride);
    nw::gfx::cmd_bind_index_buffer(cmd, prim.indices, prim.index_stride);
    nw::gfx::cmd_bind_resources(cmd, pipeline, scene_uniforms,
        plt_palette_storage(render_ctx, dummy_storage),
        bone_span.buffer.valid() ? bone_span : dummy_storage,
        forward_plus_bindings.cluster_headers,
        forward_plus_bindings.cluster_light_indices,
        forward_plus_bindings.lights,
        surface_uniforms);
    nw::gfx::cmd_draw_indexed(cmd, prim.index_count);
    return true;
}

nw::gfx::StorageSpan upload_render_model_bones(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, const Primitive& prim,
    const RenderModelSkinMatrices& skin_matrices)
{
    std::array<glm::mat4, kModelMaxSkinBones> bones{};
    uint32_t bone_count = 0;
    if (!fill_render_model_bones(bones, model, prim, skin_matrices, bone_count)) {
        return {};
    }
    return render_ctx.gpu->upload_bones(cmd, bones.data(), bone_count);
}

SurfaceConstants make_render_model_shadow_surface_constants(const Material& material) noexcept
{
    SurfaceConstants surf{};
    surf.albedo = material.albedo;
    surf.albedo_index = material.albedo_index;
    surf.alpha_mode = static_cast<uint32_t>(material.alpha_mode);
    surf.alpha_cutoff = material.alpha_cutoff;
    surf.double_sided = material.double_sided ? 1u : 0u;
    surf.color_key_threshold = material.color_key_threshold;
    apply_render_model_material_plt_constants(surf, material);
    return surf;
}

void apply_render_model_shadow_scene_constants(
    const ModelRenderContext& render_ctx,
    const RenderContext& ctx,
    SceneConstants& constants)
{
    if (ctx.shadow.enabled) {
        constants.scene_shadow_world_to_shadow = ctx.shadow.world_to_shadow;
        constants.scene_shadow_split_distances = glm::vec4(
            ctx.shadow.split_distances[0],
            ctx.shadow.split_distances[1],
            ctx.shadow.split_distances[2],
            0.0f);
        for (size_t i = 0; i < kShadowCascadeCount; ++i) {
            constants.scene_shadow_texture_indices[static_cast<glm::length_t>(i)]
                = nw::gfx::get_bindless_texture_index(render_ctx.gfx, ctx.shadow.depth_textures[i]);
        }
        constants.scene_shadow_strength = ctx.shadow.strength;
        constants.scene_shadow_debug_mode = ctx.shadow.debug_mode;
    }

    const auto& local_shadows = ctx.local_shadows;
    if (local_shadows.count == 0) {
        return;
    }

    const uint32_t count = std::min(local_shadows.count, kLocalShadowCount);
    for (uint32_t i = 0; i < count; ++i) {
        constants.scene_local_shadow_world_to_shadow[i] = local_shadows.world_to_shadow[i];
        constants.scene_local_shadow_texture_indices[static_cast<glm::length_t>(i)]
            = nw::gfx::get_bindless_texture_index(render_ctx.gfx, local_shadows.depth_textures[i]);
    }
    constants.scene_local_shadow_params = glm::vec4(
        local_shadows.strength, static_cast<float>(count), 0.0f, 0.0f);
}

void render_render_model_shadow_primitive(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model,
    const Primitive& prim,
    const Material& material,
    const SceneConstants& base_sc,
    nw::gfx::StorageSpan dummy_storage,
    const glm::mat4& world,
    const glm::mat4& normal_matrix,
    const RenderModelSkinMatrices& skin_matrices)
{
    if (!prim.vertices.valid() || !prim.indices.valid() || prim.index_count == 0
        || is_translucent_material(material.alpha_mode)) {
        return;
    }

    const auto mesh_kind = prim.skinned ? ModelPipelineMeshKind::pbr_skinned : ModelPipelineMeshKind::pbr_static;
    const auto pipeline = model_pipeline_for(
        render_ctx, mesh_kind, material.alpha_mode, false, ModelPipelinePass::shadow);
    if (!pipeline.valid()) {
        return;
    }

    uint32_t vertex_stride = sizeof(Vertex);
    nw::gfx::StorageSpan bone_span{};
    if (prim.skinned) {
        vertex_stride = sizeof(SkinnedVertex);
        bone_span = upload_render_model_bones(render_ctx, cmd, model, prim, skin_matrices);
        if (!bone_span.buffer.valid()) {
            return;
        }
    }

    SceneConstants sc = base_sc;
    sc.model = world;
    sc.normal_matrix = normal_matrix;

    const SurfaceConstants surf = make_render_model_shadow_surface_constants(material);
    auto scene_uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(SceneConstants));
    auto surface_uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(SurfaceConstants));
    if (!scene_uniforms.data || !surface_uniforms.data) {
        return;
    }
    std::memcpy(scene_uniforms.data, &sc, sizeof(sc));
    std::memcpy(surface_uniforms.data, &surf, sizeof(surf));

    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_vertex_buffer(cmd, prim.vertices, vertex_stride);
    nw::gfx::cmd_bind_index_buffer(cmd, prim.indices, prim.index_stride);
    nw::gfx::cmd_bind_resources(cmd, pipeline, scene_uniforms,
        dummy_storage,
        bone_span.buffer.valid() ? bone_span : dummy_storage,
        dummy_storage,
        dummy_storage,
        dummy_storage,
        surface_uniforms);
    nw::gfx::cmd_draw_indexed(cmd, prim.index_count);
}

void add_prepared_render_model_surface_submission_stats(
    PreparedRenderModelSurfaceSubmissionStats& stats,
    RenderPassSelection pass,
    uint32_t submitted_surface_count,
    uint32_t dropped_invalid_surface_count,
    const PreparedRenderModelSkinBindingStats& skin_bindings) noexcept
{
    add_saturating(stats.submitted_surface_count, submitted_surface_count);
    add_saturating(stats.dropped_invalid_surface_count, dropped_invalid_surface_count);
    add_prepared_render_model_skin_binding_stats(stats.skin_bindings, skin_bindings);
    if (submitted_surface_count == 0) {
        return;
    }

    add_saturating(stats.submitted_run_count, 1u);
    switch (pass) {
    case RenderPassSelection::opaque_cutout:
        add_saturating(stats.opaque_cutout_submitted_run_count, 1u);
        break;
    case RenderPassSelection::water:
        add_saturating(stats.water_submitted_run_count, 1u);
        break;
    case RenderPassSelection::transparent:
        add_saturating(stats.transparent_submitted_run_count, 1u);
        break;
    case RenderPassSelection::all:
        add_saturating(stats.all_submitted_run_count, 1u);
        break;
    }
}

} // namespace

void render_render_model_with_root(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, const glm::mat4& model_root, const RenderContext& ctx,
    RenderPassSelection pass, const ModelInstance* instance)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !cmd || model.primitives.empty()) {
        return;
    }
    const auto pbr_static_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::pbr_static, MaterialMode::opaque);
    if (!pbr_static_pipeline.valid()) {
        return;
    }

    SceneConstants base_sc = make_scene_constants(ctx, render_model_lighting(ctx));
    apply_render_model_shadow_scene_constants(render_ctx, ctx, base_sc);
    // The common PBR shaders only read scene uniforms, bones, the forward+
    // storage buffers and surface uniforms. The dummy buffer owns unused
    // storage slots while the bind protocol is still shared with legacy shaders.
    const nw::gfx::StorageSpan dummy_storage{render_ctx.gpu->dummy_storage_buffer()};
    const auto forward_plus_bindings = make_forward_plus_storage_bindings(ctx.forward_plus, dummy_storage);

    for (const auto& prim : model.primitives) {
        if (prim.material >= model.materials.size()) {
            continue;
        }
        const auto& material = model.materials[prim.material];
        if (!render_model_material_visible_in_pass(material.alpha_mode, pass)) {
            continue;
        }

        const RenderModelSkinMatrices primitive_skin_matrices = source_skin_matrices_for_primitive(
            instance,
            prim.skin);
        const glm::mat4 world = model_instance_primitive_world_transform(instance, model_root, prim);
        const glm::mat4 normal_matrix = make_normal_matrix(world);
        render_render_model_primitive(render_ctx, cmd, model, prim, material, ctx, base_sc,
            dummy_storage, forward_plus_bindings, world, normal_matrix, primitive_skin_matrices);
    }
}

void collect_prepared_render_model_surface_packets(
    PreparedRenderModelSurfacePacketList& out,
    const RenderModel& model,
    std::span<const PreparedModelSurfaceDraw> surfaces,
    RenderPassSelection pass,
    const PreparedRenderModelSkinTable* skin_table,
    const ModelMaterialOverrideStore* material_overrides)
{
    out.clear();
    out.stats.input_surface_count = static_cast<uint32_t>(
        std::min<size_t>(surfaces.size(), std::numeric_limits<uint32_t>::max()));
    out.surface_indices.reserve(surfaces.size());

    for (size_t surface_index = 0; surface_index < surfaces.size(); ++surface_index) {
        const auto& surface = surfaces[surface_index];
        if (!render_model_material_visible_in_pass(surface.material_mode, pass)) {
            add_saturating(out.stats.pass_filtered_surface_count, 1u);
            continue;
        }

        add_saturating(out.stats.candidate_surface_count, 1u);

        if (surface_index > std::numeric_limits<uint32_t>::max()) {
            add_saturating(out.stats.invalid_surface_index_count, 1u);
            add_saturating(out.stats.invalid_surface_count, 1u);
            continue;
        }

        if (!prepared_model_surface_is_render_model(surface)) {
            add_saturating(out.stats.non_render_model_surface_count, 1u);
            add_saturating(out.stats.invalid_surface_count, 1u);
            continue;
        }
        if (surface.instance_source_index == kInvalidPreparedModelDrawIndex) {
            add_saturating(out.stats.invalid_source_index_count, 1u);
            add_saturating(out.stats.invalid_surface_count, 1u);
            continue;
        }
        if (surface.source_draw_index >= model.primitives.size()) {
            add_saturating(out.stats.invalid_source_draw_index_count, 1u);
            add_saturating(out.stats.invalid_surface_count, 1u);
            continue;
        }

        const auto& primitive = model.primitives[surface.source_draw_index];
        if (primitive.material >= model.materials.size()) {
            add_saturating(out.stats.invalid_material_index_count, 1u);
            add_saturating(out.stats.invalid_surface_count, 1u);
            continue;
        }

        bool valid = true;
        if (surface.material_index != primitive.material
            || surface.skin_index != primitive.skin
            || surface.skinned != primitive.skinned) {
            add_saturating(out.stats.primitive_mismatch_count, 1u);
            valid = false;
        }

        const auto* material = prepared_surface_material(model, primitive, surface, material_overrides);
        if (!material) {
            add_saturating(out.stats.invalid_material_override_handle_count, 1u);
            valid = false;
        } else if (surface.material_mode != material->alpha_mode
            || surface.material_uses_fallback != material->material_uses_fallback
            || surface.material_payload != prepared_render_model_material_payload_kind(*material)) {
            add_saturating(out.stats.material_mismatch_count, 1u);
            valid = false;
        }

        if (!valid) {
            add_saturating(out.stats.invalid_surface_count, 1u);
            continue;
        }

        const auto skin_matrices = prepared_surface_skin_matrices(skin_table, surface);
        add_prepared_render_model_skin_binding_stats(out.stats.skin_bindings, skin_matrices.source);

        out.surface_indices.push_back(static_cast<uint32_t>(surface_index));
        add_saturating(out.stats.valid_surface_count, 1u);
    }
}

void render_prepared_render_model_surfaces(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const PreparedModelSurfaceDraw> surfaces,
    const RenderContext& ctx,
    RenderPassSelection pass, const PreparedRenderModelSkinTable* skin_table,
    const ModelMaterialOverrideStore* material_overrides,
    PreparedRenderModelSurfaceSubmissionStats* stats)
{
    if (surfaces.empty()) {
        return;
    }

    PreparedRenderModelSurfacePacketList packets;
    collect_prepared_render_model_surface_packets(packets, model, surfaces, pass, skin_table, material_overrides);

    if (!render_ctx.gfx || !render_ctx.gpu || !cmd || model.primitives.empty()
        || packets.surface_indices.empty()) {
        if (stats) {
            add_prepared_render_model_surface_submission_stats(
                *stats,
                pass,
                0u,
                packets.stats.invalid_surface_count,
                packets.stats.skin_bindings);
        }
        return;
    }

    const auto pbr_static_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::pbr_static, MaterialMode::opaque);
    if (!pbr_static_pipeline.valid()) {
        if (stats) {
            add_prepared_render_model_surface_submission_stats(
                *stats,
                pass,
                0u,
                packets.stats.invalid_surface_count,
                packets.stats.skin_bindings);
        }
        return;
    }

    SceneConstants base_sc = make_scene_constants(ctx, render_model_lighting(ctx));
    apply_render_model_shadow_scene_constants(render_ctx, ctx, base_sc);
    const nw::gfx::StorageSpan dummy_storage{render_ctx.gpu->dummy_storage_buffer()};
    const auto forward_plus_bindings = make_forward_plus_storage_bindings(ctx.forward_plus, dummy_storage);

    uint32_t submitted_surface_count = 0;
    for (const uint32_t surface_index : packets.surface_indices) {
        const auto& surface = surfaces[surface_index];
        const auto& prim = model.primitives[surface.source_draw_index];
        const auto* material = prepared_surface_material(model, prim, surface, material_overrides);
        if (!material) {
            continue;
        }
        const RenderModelSkinMatrices skin_matrices = prepared_surface_skin_matrices(skin_table, surface);
        if (render_render_model_primitive(render_ctx, cmd, model, prim, *material, ctx, base_sc,
                dummy_storage, forward_plus_bindings, surface.world, surface.normal_matrix, skin_matrices)) {
            add_saturating(submitted_surface_count, 1u);
        }
    }

    if (stats) {
        add_prepared_render_model_surface_submission_stats(
            *stats,
            pass,
            submitted_surface_count,
            packets.stats.invalid_surface_count,
            packets.stats.skin_bindings);
    }
}

void render_prepared_render_model_shadow_surfaces(const ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const RenderModel& model, std::span<const PreparedModelSurfaceDraw> surfaces,
    uint32_t range_index, const glm::mat4& light_view, const glm::mat4& light_projection,
    const PreparedRenderModelSkinTable* skin_table,
    const ModelMaterialOverrideStore* material_overrides)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !cmd || model.primitives.empty() || surfaces.empty()
        || range_index == kInvalidPreparedModelDrawIndex) {
        return;
    }

    SceneConstants base_sc{};
    base_sc.view = light_view;
    base_sc.projection = light_projection;
    const nw::gfx::StorageSpan dummy_storage{render_ctx.gpu->dummy_storage_buffer()};

    for (const auto& surface : surfaces) {
        if (surface.range_index != range_index) {
            continue;
        }
        if (!prepared_shadow_surface_matches_model(model, surface, material_overrides)) {
            continue;
        }

        const auto& prim = model.primitives[surface.source_draw_index];
        const auto* material = prepared_surface_material(model, prim, surface, material_overrides);
        if (!material) {
            continue;
        }
        const RenderModelSkinMatrices skin_matrices = prepared_surface_skin_matrices(skin_table, surface);
        render_render_model_shadow_primitive(render_ctx, cmd, model, prim, *material, base_sc,
            dummy_storage, surface.world, surface.normal_matrix, skin_matrices);
    }
}

} // namespace nw::render
