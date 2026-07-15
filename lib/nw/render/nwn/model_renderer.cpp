#include "model_renderer.hpp"

#include "model_gpu_backend.hpp"
#include "model_loader.hpp"
#include "render_asset_cache.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace nw::render::nwn {

constexpr uint32_t kSceneUniformAlignment = 256;
constexpr size_t kPartitionSortedStaticBatchThreshold = 1024;

static_assert(sizeof(PreparedStaticDrawConstants) % 16 == 0);
static_assert(sizeof(PreparedStaticShadowDrawConstants) % 16 == 0);

PreparedDrawDataCache::~PreparedDrawDataCache()
{
    clear();
}

PreparedDrawDataCache::PreparedDrawDataCache(PreparedDrawDataCache&& other) noexcept
    : buffer(other.buffer)
    , span(other.span)
    , signature(other.signature)
    , asset_cache_epoch(other.asset_cache_epoch)
    , source_draw_count(other.source_draw_count)
{
    other.buffer = {};
    other.span = {};
    other.signature = 0;
    other.asset_cache_epoch = 0;
    other.source_draw_count = std::numeric_limits<uint32_t>::max();
}

PreparedDrawDataCache& PreparedDrawDataCache::operator=(PreparedDrawDataCache&& other) noexcept
{
    if (this != &other) {
        clear();
        buffer = other.buffer;
        span = other.span;
        signature = other.signature;
        asset_cache_epoch = other.asset_cache_epoch;
        source_draw_count = other.source_draw_count;
        other.buffer = {};
        other.span = {};
        other.signature = 0;
        other.asset_cache_epoch = 0;
        other.source_draw_count = std::numeric_limits<uint32_t>::max();
    }
    return *this;
}

void PreparedDrawDataCache::clear()
{
    if (buffer.valid()) {
        nw::gfx::destroy_buffer(buffer);
    }
    buffer = {};
    span = {};
    signature = 0;
    asset_cache_epoch = 0;
    source_draw_count = std::numeric_limits<uint32_t>::max();
}

bool PreparedDrawDataCache::valid_for(
    size_t input_draw_count,
    uint64_t input_signature,
    uint64_t input_asset_cache_epoch,
    size_t bytes_per_draw) const noexcept
{
    if (input_draw_count > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const auto count = static_cast<uint32_t>(input_draw_count);
    if (bytes_per_draw != 0
        && static_cast<uint64_t>(count) > std::numeric_limits<uint64_t>::max() / bytes_per_draw) {
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(count) * bytes_per_draw;
    return span.buffer.valid()
        && span.buffer == buffer
        && signature == input_signature
        && asset_cache_epoch == input_asset_cache_epoch
        && source_draw_count == count
        && static_cast<uint64_t>(span.size) >= bytes;
}

struct SceneUniformBatch {
    nw::gfx::UniformSpan span;
    uint32_t stride = 0;
    uint32_t count = 0;
    uint32_t next = 0;
};

static uint32_t align_up_u32(uint32_t value, uint32_t alignment) noexcept
{
    return ((value + alignment - 1) / alignment) * alignment;
}

static SceneUniformBatch allocate_scene_uniform_batch(nw::gfx::Context* ctx, size_t count)
{
    if (!ctx || count < 2 || count > std::numeric_limits<uint32_t>::max()) {
        return {};
    }

    const uint32_t stride = align_up_u32(
        static_cast<uint32_t>(sizeof(nw::render::SceneConstants)), kSceneUniformAlignment);
    const uint64_t total_size = static_cast<uint64_t>(stride) * (count - 1)
        + sizeof(nw::render::SceneConstants);
    if (total_size > std::numeric_limits<uint32_t>::max()) {
        return {};
    }

    auto span = nw::gfx::allocate_uniform_span(ctx, static_cast<uint32_t>(total_size), kSceneUniformAlignment);
    if (!span.data) {
        return {};
    }

    return SceneUniformBatch{
        .span = span,
        .stride = stride,
        .count = static_cast<uint32_t>(count),
    };
}

static nw::gfx::UniformSpan allocate_scene_uniform(
    nw::gfx::Context* ctx, SceneUniformBatch* batch)
{
    if (batch && batch->span.data && batch->next < batch->count) {
        nw::gfx::UniformSpan result = batch->span;
        result.offset = batch->span.offset + batch->next * batch->stride;
        result.size = static_cast<uint32_t>(sizeof(nw::render::SceneConstants));
        result.data = static_cast<uint8_t*>(batch->span.data) + batch->next * batch->stride;
        ++batch->next;
        return result;
    }
    return nw::gfx::allocate_uniform_span(ctx, sizeof(nw::render::SceneConstants));
}

static bool is_translucent_material(MaterialMode mode)
{
    return mode == MaterialMode::transparent || mode == MaterialMode::water;
}

static Bounds transform_bounds(const Bounds& bounds, const glm::mat4& transform)
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

    Bounds result{
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

static glm::mat4 make_normal_matrix(const glm::mat4& model_matrix)
{
    return glm::mat4(glm::mat3(glm::transpose(glm::inverse(model_matrix))));
}

static float distance_squared_to_bounds(const glm::vec3& point, const Bounds& bounds)
{
    const glm::vec3 closest = glm::clamp(point, bounds.min, bounds.max);
    const glm::vec3 delta = point - closest;
    return glm::dot(delta, delta);
}

static bool is_ambient_local_light(const nw::render::LocalLight& light) noexcept
{
    return light.contribution == nw::render::LocalLightContribution::ambient;
}

static float local_light_distance_squared_to_bounds(const nw::render::LocalLight& light, const Bounds& bounds)
{
    const glm::vec3 closest = glm::clamp(light.position, bounds.min, bounds.max);
    glm::vec3 delta = light.position - closest;
    if (is_ambient_local_light(light)) {
        delta.z *= std::clamp(light.vertical_scale, 0.0f, 1.0f);
    }
    return glm::dot(delta, delta);
}

static bool local_light_geometry_intersects_bounds(const nw::render::LocalLight& light, const Bounds& bounds)
{
    if (light.radius <= 1.0e-4f) {
        return false;
    }

    const float radius_squared = light.radius * light.radius;
    const float bounds_distance_squared = is_ambient_local_light(light)
        ? local_light_distance_squared_to_bounds(light, bounds)
        : distance_squared_to_bounds(light.position, bounds);
    return bounds_distance_squared < radius_squared;
}

static bool model_supports_pass(const ModelInstance& model, nw::render::RenderPassSelection pass) noexcept
{
    switch (pass) {
    case nw::render::RenderPassSelection::opaque_cutout:
        return model.has_opaque_cutout_pass();
    case nw::render::RenderPassSelection::water:
        return model.has_water_pass();
    case nw::render::RenderPassSelection::transparent:
        return model.has_transparent_pass();
    case nw::render::RenderPassSelection::all:
        return model.material_pass_mask != 0;
    }
    return true;
}

static void collect_draws_recursive(std::vector<PreparedDrawItem>& draws, Node* node,
    const glm::mat4& parent_transform, uint64_t parent_revision, const glm::mat4& model_root,
    uint64_t root_revision, const ModelInstance& model)
{
    const auto& model_transform = node->refresh_model_transform_cache(parent_transform, parent_revision);
    const uint64_t model_revision = node->model_transform_cache_revision();
    const glm::mat4 world = model_root * model_transform;

    if (node->is_mesh) {
        auto* mesh = static_cast<Mesh*>(node);
        if (mesh->vertices.valid() && mesh->index_count > 0) {
            const Bounds world_bounds = transform_bounds(mesh->local_bounds, world);
            const glm::vec3 world_center = world_bounds.center();
            const glm::mat4& normal_matrix = mesh->is_skin
                ? model.refresh_root_normal_matrix_cache(model_root, root_revision)
                : mesh->refresh_normal_matrix_cache(world, model_revision, root_revision);
            draws.push_back(PreparedDrawItem{
                .mesh = mesh,
                .world = world,
                .normal_matrix = normal_matrix,
                .model_root = model_root,
                .sort_position = world_center,
                .light_bounds = world_bounds,
                .order = draws.size(),
            });
        }
    }

    for (auto& child : node->children_) {
        collect_draws_recursive(draws, child, model_transform, model_revision, model_root, root_revision, model);
    }
}

void collect_prepared_draws(std::vector<PreparedDrawItem>& draws, const ModelInstance& model,
    const glm::mat4& model_root)
{
    static const glm::mat4 identity{1.0f};
    const uint64_t root_revision = model.root_transform_cache_revision(model_root);
    for (const auto& node : model.nodes_) {
        if (!node->parent_) {
            collect_draws_recursive(draws, node.get(), identity, 0, model_root, root_revision, model);
            break;
        }
    }
}

void collect_local_light_indices_for_bounds(
    std::vector<uint32_t>& indices,
    std::span<const nw::render::LocalLight> lights,
    const nw::render::Bounds& bounds)
{
    for (size_t i = 0; i < lights.size(); ++i) {
        if (i > std::numeric_limits<uint32_t>::max()) {
            break;
        }
        if (local_light_geometry_intersects_bounds(lights[i], bounds)) {
            indices.push_back(static_cast<uint32_t>(i));
        }
    }
}

static void ensure_mesh_texture(ModelRenderContext& render_ctx, Mesh& mesh)
{
    const uint64_t cache_epoch = render_ctx.assets->epoch();
    if (mesh.asset_cache_epoch != cache_epoch) {
        mesh.texture = {};
        mesh.texture_index = nw::gfx::kInvalidBindlessTextureIndex;
        mesh.normal_texture = {};
        mesh.normal_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
        mesh.surface_texture = {};
        mesh.surface_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
        mesh.emissive_texture = {};
        mesh.emissive_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
        mesh.specular_texture = {};
        mesh.specular_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
        mesh.asset_cache_epoch = cache_epoch;
    }
    if (mesh.texture.valid() || mesh.bitmap_name.empty()) {
        return;
    }

    if (mesh.uses_plt) {
        mesh.texture = render_ctx.assets->get_or_load_raw_plt_texture(nw::Resref{mesh.bitmap_name});
    } else {
        const bool premultiply_alpha = mesh.material_mode == MaterialMode::transparent;
        mesh.texture = render_ctx.assets->get_or_load_texture(
            nw::Resref{mesh.bitmap_name}, premultiply_alpha, render_ctx.gpu->fallback_texture());
    }
    mesh.texture_index = nw::gfx::get_bindless_texture_index(render_ctx.gfx, mesh.texture);
}

static nw::gfx::BindlessTextureIndex bindless_index_or_invalid(
    nw::gfx::Context* ctx, nw::gfx::Handle<nw::gfx::Texture> texture)
{
    return texture.valid() ? nw::gfx::get_bindless_texture_index(ctx, texture) : nw::gfx::kInvalidBindlessTextureIndex;
}

static void ensure_mesh_material_textures(ModelRenderContext& render_ctx, Mesh& mesh)
{
    ensure_mesh_texture(render_ctx, mesh);
    if (!mesh.normal_texture.valid() && !mesh.normal_map_name.empty()) {
        mesh.normal_texture = render_ctx.assets->get_or_load_linear_texture(nw::Resref{mesh.normal_map_name});
        mesh.normal_texture_index = bindless_index_or_invalid(render_ctx.gfx, mesh.normal_texture);
    }
    if (!mesh.surface_texture.valid() && !mesh.roughness_map_name.empty()) {
        mesh.surface_texture = render_ctx.assets->get_or_load_roughness_surface_texture(nw::Resref{mesh.roughness_map_name});
        mesh.surface_texture_index = bindless_index_or_invalid(render_ctx.gfx, mesh.surface_texture);
    }
    if (!mesh.emissive_texture.valid() && !mesh.emissive_map_name.empty()) {
        const auto fallback = render_ctx.gpu->fallback_texture();
        mesh.emissive_texture = render_ctx.assets->get_or_load_texture(nw::Resref{mesh.emissive_map_name}, false, fallback);
        mesh.emissive_texture_index = mesh.emissive_texture != fallback
            ? bindless_index_or_invalid(render_ctx.gfx, mesh.emissive_texture)
            : nw::gfx::kInvalidBindlessTextureIndex;
    }
    if (!mesh.specular_texture.valid() && !mesh.specular_map_name.empty()) {
        mesh.specular_texture = render_ctx.assets->get_or_load_linear_texture(nw::Resref{mesh.specular_map_name});
        mesh.specular_texture_index = bindless_index_or_invalid(render_ctx.gfx, mesh.specular_texture);
    }
}

static glm::uvec4 material_texture_indices(const Mesh& mesh) noexcept
{
    return glm::uvec4(
        mesh.normal_texture_index,
        mesh.surface_texture_index,
        mesh.emissive_texture_index,
        mesh.specular_texture_index);
}

static nw::gfx::StorageSpan plt_palette_storage(const ModelRenderContext& render_ctx) noexcept
{
    return render_ctx.gpu
        ? nw::gfx::StorageSpan{render_ctx.gpu->plt_palette_buffer()}
        : nw::gfx::StorageSpan{};
}

static nw::gfx::Handle<nw::gfx::Pipeline> model_pipeline_for(
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

static void draw_mesh_item(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const PreparedDrawItem& item, const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index, bool offscreen_pass,
    const nw::gfx::UniformSpan& shadow_uniforms,
    SceneUniformBatch* scene_uniform_batch,
    const nw::render::ForwardPlusResources* forward_plus = nullptr)
{
    auto* mesh = item.mesh;
    if (!mesh) {
        return;
    }

    ensure_mesh_material_textures(render_ctx, *mesh);

    auto pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::opaque, offscreen_pass);
    if (offscreen_pass && !pipeline.valid()) {
        pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::opaque);
    }
    auto vertex_stride = sizeof(Vertex);
    const bool is_translucent = is_translucent_material(mesh->material_mode);
    const bool is_water = mesh->material_mode == MaterialMode::water;
    const bool forward_plus_ready = forward_plus_resources_ready(forward_plus);
    const nw::gfx::StorageSpan plt_storage = plt_palette_storage(render_ctx);
    const nw::gfx::StorageSpan dummy_storage = plt_storage;
    const nw::gfx::StorageSpan cluster_headers = forward_plus_ready ? forward_plus->cluster_header_buffer : dummy_storage;
    const nw::gfx::StorageSpan cluster_light_indices = forward_plus_ready ? forward_plus->cluster_light_index_buffer : dummy_storage;
    const nw::gfx::StorageSpan lights = forward_plus_ready ? forward_plus->light_buffer : dummy_storage;
    const auto populate_common = [&](auto& sc) {
        const glm::mat4 model_matrix = mesh->is_skin ? item.model_root : item.world;
        sc.model = model_matrix;
        sc.normal_matrix = item.normal_matrix;
        sc.texture_index = nw::gfx::bindless_texture_index_valid(mesh->texture_index) ? mesh->texture_index : fallback_texture_index;
        sc.alpha_cutout = mesh->material_mode == MaterialMode::cutout ? 1 : 0;
        sc.alpha_cutout_threshold = mesh->alpha_cutout_threshold;
        sc.two_sided_lighting = mesh->two_sided_lighting ? 1u : 0u;
        sc.pad_alpha.x = mesh->opacity;
        sc.color_key_rg = glm::vec2(mesh->color_key.x, mesh->color_key.y);
        sc.color_key_b = mesh->color_key.z;
        sc.color_key_threshold = mesh->color_key_threshold;
        sc.plt_enabled = mesh->uses_plt ? 1u : 0u;
        sc.plt_colors0 = glm::uvec4(mesh->plt_colors.data[0], mesh->plt_colors.data[1], mesh->plt_colors.data[2], mesh->plt_colors.data[3]);
        sc.plt_colors1 = glm::uvec4(mesh->plt_colors.data[4], mesh->plt_colors.data[5], mesh->plt_colors.data[6], mesh->plt_colors.data[7]);
        sc.plt_colors2 = glm::uvec4(mesh->plt_colors.data[8], mesh->plt_colors.data[9], 0u, 0u);
        sc.emissive_color = glm::vec4(mesh->emissive, 0.0f);
        sc.material_params = glm::vec4(mesh->roughness, mesh->specular_strength, 0.0f, 0.0f);
        sc.material_texture_indices = material_texture_indices(*mesh);
    };

    const auto skinned_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::skinned, MaterialMode::opaque);
    if (mesh->is_skin && skinned_pipeline.valid()) {
        nw::render::SceneConstants sc = base_sc;
        populate_common(sc);
        if (is_translucent) {
            pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::skinned, mesh->material_mode, offscreen_pass);
            if (offscreen_pass && !pipeline.valid()) {
                pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::skinned, mesh->material_mode);
            }
            if (!pipeline.valid()) {
                pipeline = skinned_pipeline;
            }
        } else {
            pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::skinned, mesh->material_mode, offscreen_pass);
            if (offscreen_pass && !pipeline.valid()) {
                pipeline = skinned_pipeline;
            }
        }
        vertex_stride = sizeof(SkinnedVertex);

        auto* skin = static_cast<SkinMesh*>(mesh);
        std::array<glm::mat4, nw::render::kModelMaxSkinBones> bones{};
        for (auto& bone : bones) {
            bone = glm::mat4(1.0f);
        }
        skin->fill_skin_matrices(bones.data(), bones.size());

        auto bone_span = render_ctx.gpu->upload_bones(cmd, bones.data(), static_cast<uint32_t>(bones.size()));
        auto uniforms = allocate_scene_uniform(render_ctx.gfx, scene_uniform_batch);
        if (!uniforms.data || !bone_span.buffer.valid()) {
            return;
        }
        std::memcpy(uniforms.data, &sc, sizeof(sc));
        nw::gfx::cmd_bind_pipeline(cmd, pipeline);
        nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
        nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
        nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
            plt_storage, bone_span, cluster_headers, cluster_light_indices, lights, shadow_uniforms);
        nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
        return;
    } else if (is_water) {
        auto water_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::water, offscreen_pass);
        if (offscreen_pass && !water_pipeline.valid()) {
            water_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::water);
        }
        if (!water_pipeline.valid()) {
            water_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::transparent, offscreen_pass);
        }
        if (offscreen_pass && !water_pipeline.valid()) {
            water_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::transparent);
        }
        if (water_pipeline.valid()) {
            pipeline = water_pipeline;
        }
    } else if (is_translucent) {
        auto transparent_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::transparent, offscreen_pass);
        if (offscreen_pass && !transparent_pipeline.valid()) {
            transparent_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::transparent);
        }
        if (transparent_pipeline.valid()) {
            pipeline = transparent_pipeline;
        }
    }

    nw::render::SceneConstants sc = base_sc;
    populate_common(sc);

    auto uniforms = allocate_scene_uniform(render_ctx.gfx, scene_uniform_batch);
    if (!uniforms.data) {
        return;
    }

    std::memcpy(uniforms.data, &sc, sizeof(sc));
    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
    nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
    nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
        plt_storage, dummy_storage, cluster_headers, cluster_light_indices, lights, shadow_uniforms);
    nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
}

static void draw_shadow_mesh_item(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const PreparedDrawItem& item, const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index,
    SceneUniformBatch* scene_uniform_batch = nullptr)
{
    auto* mesh = item.mesh;
    if (!mesh || is_translucent_material(mesh->material_mode)) {
        return;
    }

    const bool is_cutout = mesh->material_mode == MaterialMode::cutout;
    if (is_cutout) {
        ensure_mesh_texture(render_ctx, *mesh);
    }

    auto pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, mesh->material_mode, false, ModelPipelinePass::shadow);
    if (is_cutout && !pipeline.valid()) {
        pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::opaque, false, ModelPipelinePass::shadow);
    }
    auto vertex_stride = sizeof(Vertex);

    const auto populate_common = [&](auto& sc) {
        const glm::mat4 model_matrix = mesh->is_skin ? item.model_root : item.world;
        sc.model = model_matrix;
        sc.normal_matrix = item.normal_matrix;
        sc.texture_index = nw::gfx::bindless_texture_index_valid(mesh->texture_index) ? mesh->texture_index : fallback_texture_index;
        sc.alpha_cutout = mesh->material_mode == MaterialMode::cutout ? 1 : 0;
        sc.alpha_cutout_threshold = mesh->alpha_cutout_threshold;
        sc.pad_alpha.x = mesh->opacity;
    };

    const auto skinned_shadow_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::skinned, MaterialMode::opaque, false, ModelPipelinePass::shadow);
    if (mesh->is_skin && skinned_shadow_pipeline.valid()) {
        nw::render::SceneConstants sc = base_sc;
        populate_common(sc);
        pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::skinned, mesh->material_mode, false, ModelPipelinePass::shadow);
        if (is_cutout && !pipeline.valid()) {
            pipeline = skinned_shadow_pipeline;
        }
        vertex_stride = sizeof(SkinnedVertex);

        auto* skin = static_cast<SkinMesh*>(mesh);
        std::array<glm::mat4, nw::render::kModelMaxSkinBones> bones{};
        for (auto& bone : bones) {
            bone = glm::mat4(1.0f);
        }
        skin->fill_skin_matrices(bones.data(), bones.size());

        auto bone_span = render_ctx.gpu->upload_bones(cmd, bones.data(), static_cast<uint32_t>(bones.size()));
        auto uniforms = allocate_scene_uniform(render_ctx.gfx, scene_uniform_batch);
        if (!uniforms.data || !bone_span.buffer.valid()) {
            return;
        }
        std::memcpy(uniforms.data, &sc, sizeof(sc));
        nw::gfx::cmd_bind_pipeline(cmd, pipeline);
        nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
        nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
        nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
            plt_palette_storage(render_ctx), bone_span);
        nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
        return;
    }

    nw::render::SceneConstants sc = base_sc;
    populate_common(sc);
    auto uniforms = allocate_scene_uniform(render_ctx.gfx, scene_uniform_batch);
    if (!uniforms.data) {
        return;
    }
    std::memcpy(uniforms.data, &sc, sizeof(sc));
    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(vertex_stride));
    nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
    nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms);
    nw::gfx::cmd_draw_indexed(cmd, mesh->index_count);
}

static void draw_shadow_cached_mesh(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    Mesh* mesh,
    const glm::mat4& model_root,
    const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index,
    SceneUniformBatch* scene_uniform_batch = nullptr)
{
    if (!mesh || !mesh->vertices.valid() || mesh->index_count == 0 || is_translucent_material(mesh->material_mode)) {
        return;
    }

    const auto& model_transform = mesh->refresh_model_transform_cache();
    const glm::mat4 world = model_root * model_transform;
    glm::mat4 normal_matrix{1.0f};
    if (auto* owner = mesh->owner_) {
        const uint64_t root_revision = owner->root_transform_cache_revision(model_root);
        normal_matrix = mesh->is_skin
            ? owner->refresh_root_normal_matrix_cache(model_root, root_revision)
            : mesh->refresh_normal_matrix_cache(world, mesh->model_transform_cache_revision(), root_revision);
    } else if (mesh->is_skin) {
        normal_matrix = make_normal_matrix(model_root);
    } else {
        normal_matrix = make_normal_matrix(world);
    }

    const PreparedDrawItem item{
        .mesh = mesh,
        .world = world,
        .normal_matrix = normal_matrix,
        .model_root = model_root,
    };
    draw_shadow_mesh_item(render_ctx, cmd, item, base_sc, fallback_texture_index, scene_uniform_batch);
}

static nw::gfx::Handle<nw::gfx::Pipeline> static_batched_pipeline_for(
    ModelRenderContext& render_ctx, MaterialMode mode, bool offscreen_pass, bool depth_prepass = false)
{
    const auto pass = depth_prepass ? ModelPipelinePass::depth_prepass : ModelPipelinePass::color;
    auto pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_batched, mode, offscreen_pass, pass);
    if (!depth_prepass && offscreen_pass && !pipeline.valid()) {
        pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_batched, mode);
    }
    return pipeline;
}

static bool supports_static_batched_draw(const PreparedDrawItem* draw, MaterialMode mode) noexcept
{
    return draw && draw->mesh && !draw->mesh->is_skin && mode != MaterialMode::water
        && draw->mesh->material_mode == mode;
}

static bool supports_static_batched_shadow_draw(const PreparedDrawItem* draw) noexcept
{
    return draw && draw->mesh && !draw->mesh->is_skin
        && !is_translucent_material(draw->mesh->material_mode);
}

static bool static_batched_geometry_less(const PreparedDrawItem* lhs, const PreparedDrawItem* rhs) noexcept
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

static bool static_batched_geometry_matches(const PreparedDrawItem* lhs, const PreparedDrawItem* rhs) noexcept
{
    return lhs && rhs && lhs->mesh && rhs->mesh
        && lhs->mesh->vertices == rhs->mesh->vertices
        && lhs->mesh->indices == rhs->mesh->indices
        && lhs->mesh->index_count == rhs->mesh->index_count;
}

static bool static_batched_indirect_geometry_matches(const PreparedDrawItem* lhs, const PreparedDrawItem* rhs) noexcept
{
    if (!lhs || !rhs) {
        return false;
    }
    const auto& lhs_geometry = lhs->static_area_geometry;
    const auto& rhs_geometry = rhs->static_area_geometry;
    return lhs_geometry.valid()
        && rhs_geometry.valid()
        && lhs_geometry.vertices == rhs_geometry.vertices
        && lhs_geometry.indices == rhs_geometry.indices
        && lhs_geometry.index_count == rhs_geometry.index_count
        && lhs_geometry.first_index == rhs_geometry.first_index
        && lhs_geometry.vertex_offset == rhs_geometry.vertex_offset;
}

bool build_prepared_indirect_draw_commands(
    std::vector<nw::gfx::IndexedIndirectDrawCommand>& command_storage,
    PreparedIndirectDrawCommands& out,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode)
{
    command_storage.clear();
    out = {};
    if (draws.empty() || draws.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    nw::gfx::IndexedIndirectDrawStats command_stats{};
    uint32_t instance_count_total = 0;
    uint32_t max_instances_per_draw = 0;
    uint32_t draw_index = 0;
    const auto source_draw_count = static_cast<uint32_t>(draws.size());
    command_storage.reserve(draws.size());

    while (draw_index < source_draw_count) {
        const auto* draw = draws[draw_index];
        if (!supports_static_batched_draw(draw, mode) || !draw->static_area_geometry.valid()) {
            ++draw_index;
            continue;
        }

        const auto& geometry = draw->static_area_geometry;
        if (!vertices.valid() && !indices.valid()) {
            vertices = geometry.vertices;
            indices = geometry.indices;
        } else if (vertices != geometry.vertices || indices != geometry.indices) {
            command_storage.clear();
            out = {};
            return false;
        }

        uint32_t instance_count = 1;
        while (draw_index + instance_count < source_draw_count
            && supports_static_batched_draw(draws[draw_index + instance_count], mode)
            && static_batched_indirect_geometry_matches(draw, draws[draw_index + instance_count])) {
            ++instance_count;
        }

        command_storage.push_back(nw::gfx::IndexedIndirectDrawCommand{
            .index_count = geometry.index_count,
            .instance_count = instance_count,
            .first_index = geometry.first_index,
            .vertex_offset = geometry.vertex_offset,
            .first_instance = draw_index,
        });
        command_stats.index_count += geometry.index_count;
        command_stats.instance_count += instance_count;
        instance_count_total += instance_count;
        max_instances_per_draw = std::max(max_instances_per_draw, instance_count);
        draw_index += instance_count;
    }

    if (command_storage.empty() || !vertices.valid() || !indices.valid()) {
        out = {};
        return false;
    }

    out = PreparedIndirectDrawCommands{
        .commands = command_storage,
        .gpu_commands = {},
        .vertices = vertices,
        .indices = indices,
        .stats = command_stats,
        .source_draw_count = source_draw_count,
        .instance_count = instance_count_total,
        .command_count = static_cast<uint32_t>(command_storage.size()),
        .max_instances_per_draw = max_instances_per_draw,
    };
    return true;
}

bool build_prepared_static_material_indirect_draw_commands(
    std::vector<nw::gfx::IndexedIndirectDrawCommand>& command_storage,
    PreparedIndirectDrawCommands& out,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode)
{
    command_storage.clear();
    out = {};
    if (draws.empty() || draws.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    nw::gfx::IndexedIndirectDrawStats command_stats{};
    uint32_t instance_count_total = 0;
    uint32_t max_instances_per_draw = 0;
    uint32_t draw_index = 0;
    const auto source_draw_count = static_cast<uint32_t>(draws.size());
    command_storage.reserve(draws.size());

    while (draw_index < source_draw_count) {
        const auto* draw = draws[draw_index];
        if (!supports_static_batched_draw(draw, mode) || !draw->static_area_geometry.valid()
            || draw->static_material_index == kInvalidPreparedDrawIndex) {
            ++draw_index;
            continue;
        }

        const auto& geometry = draw->static_area_geometry;
        if (!vertices.valid() && !indices.valid()) {
            vertices = geometry.vertices;
            indices = geometry.indices;
        } else if (vertices != geometry.vertices || indices != geometry.indices) {
            command_storage.clear();
            out = {};
            return false;
        }

        const uint32_t first_instance = draw->static_material_index;
        uint32_t instance_count = 1;
        while (draw_index + instance_count < source_draw_count) {
            const auto* next = draws[draw_index + instance_count];
            if (!supports_static_batched_draw(next, mode)
                || !static_batched_indirect_geometry_matches(draw, next)
                || next->static_material_index != first_instance + instance_count) {
                break;
            }
            ++instance_count;
        }

        command_storage.push_back(nw::gfx::IndexedIndirectDrawCommand{
            .index_count = geometry.index_count,
            .instance_count = instance_count,
            .first_index = geometry.first_index,
            .vertex_offset = geometry.vertex_offset,
            .first_instance = first_instance,
        });
        command_stats.index_count += geometry.index_count;
        command_stats.instance_count += instance_count;
        instance_count_total += instance_count;
        max_instances_per_draw = std::max(max_instances_per_draw, instance_count);
        draw_index += instance_count;
    }

    if (command_storage.empty() || !vertices.valid() || !indices.valid()) {
        out = {};
        return false;
    }

    out = PreparedIndirectDrawCommands{
        .commands = command_storage,
        .gpu_commands = {},
        .vertices = vertices,
        .indices = indices,
        .stats = command_stats,
        .source_draw_count = source_draw_count,
        .instance_count = instance_count_total,
        .command_count = static_cast<uint32_t>(command_storage.size()),
        .max_instances_per_draw = max_instances_per_draw,
    };
    return true;
}

bool build_prepared_shadow_indirect_draw_commands(
    std::vector<nw::gfx::IndexedIndirectDrawCommand>& command_storage,
    PreparedIndirectDrawCommands& out,
    std::span<const PreparedDrawItem* const> draws)
{
    command_storage.clear();
    out = {};
    if (draws.empty() || draws.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    nw::gfx::IndexedIndirectDrawStats command_stats{};
    uint32_t instance_count_total = 0;
    uint32_t max_instances_per_draw = 0;
    uint32_t draw_index = 0;
    const auto source_draw_count = static_cast<uint32_t>(draws.size());
    command_storage.reserve(draws.size());

    while (draw_index < source_draw_count) {
        const auto* draw = draws[draw_index];
        if (!supports_static_batched_shadow_draw(draw) || !draw->static_area_geometry.valid()) {
            ++draw_index;
            continue;
        }

        const auto& geometry = draw->static_area_geometry;
        if (!vertices.valid() && !indices.valid()) {
            vertices = geometry.vertices;
            indices = geometry.indices;
        } else if (vertices != geometry.vertices || indices != geometry.indices) {
            command_storage.clear();
            out = {};
            return false;
        }

        uint32_t instance_count = 1;
        while (draw_index + instance_count < source_draw_count
            && supports_static_batched_shadow_draw(draws[draw_index + instance_count])
            && static_batched_indirect_geometry_matches(draw, draws[draw_index + instance_count])) {
            ++instance_count;
        }

        command_storage.push_back(nw::gfx::IndexedIndirectDrawCommand{
            .index_count = geometry.index_count,
            .instance_count = instance_count,
            .first_index = geometry.first_index,
            .vertex_offset = geometry.vertex_offset,
            .first_instance = draw_index,
        });
        command_stats.index_count += geometry.index_count;
        command_stats.instance_count += instance_count;
        instance_count_total += instance_count;
        max_instances_per_draw = std::max(max_instances_per_draw, instance_count);
        draw_index += instance_count;
    }

    if (command_storage.empty() || !vertices.valid() || !indices.valid()) {
        out = {};
        return false;
    }

    out = PreparedIndirectDrawCommands{
        .commands = command_storage,
        .gpu_commands = {},
        .vertices = vertices,
        .indices = indices,
        .stats = command_stats,
        .source_draw_count = source_draw_count,
        .instance_count = instance_count_total,
        .command_count = static_cast<uint32_t>(command_storage.size()),
        .max_instances_per_draw = max_instances_per_draw,
    };
    return true;
}

static void add_saturating(uint32_t& target, size_t value) noexcept
{
    const uint64_t next = static_cast<uint64_t>(target) + static_cast<uint64_t>(value);
    target = static_cast<uint32_t>(std::min<uint64_t>(next, std::numeric_limits<uint32_t>::max()));
}

static void add_saturating(uint32_t& target, uint32_t value) noexcept
{
    add_saturating(target, static_cast<size_t>(value));
}

static void add_saturating(uint64_t& target, size_t value) noexcept
{
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    const uint64_t add = static_cast<uint64_t>(value);
    target = max - target < add ? max : target + add;
}

static PreparedStaticDrawConstants make_static_draw_constants(
    ModelRenderContext& render_ctx,
    const PreparedDrawItem& item,
    nw::gfx::BindlessTextureIndex fallback_texture_index)
{
    PreparedStaticDrawConstants result{};
    auto* mesh = item.mesh;
    if (!mesh) {
        return result;
    }

    ensure_mesh_material_textures(render_ctx, *mesh);
    const glm::mat4 model_matrix = item.world;
    result.model = model_matrix;
    result.normal_matrix = item.normal_matrix;
    result.color_key_threshold = glm::vec4(
        mesh->color_key.x,
        mesh->color_key.y,
        mesh->color_key.z,
        mesh->color_key_threshold);
    result.pad_alpha = glm::vec4(mesh->opacity, 0.0f, 0.0f, 0.0f);
    result.texture_flags = glm::uvec4(
        nw::gfx::bindless_texture_index_valid(mesh->texture_index) ? mesh->texture_index : fallback_texture_index,
        mesh->material_mode == MaterialMode::cutout ? 1u : 0u,
        mesh->uses_plt ? 1u : 0u,
        mesh->two_sided_lighting ? 1u : 0u);
    result.alpha_params = glm::vec4(mesh->alpha_cutout_threshold, 0.0f, 0.0f, 0.0f);
    result.plt_colors0 = glm::uvec4(
        mesh->plt_colors.data[0],
        mesh->plt_colors.data[1],
        mesh->plt_colors.data[2],
        mesh->plt_colors.data[3]);
    result.plt_colors1 = glm::uvec4(
        mesh->plt_colors.data[4],
        mesh->plt_colors.data[5],
        mesh->plt_colors.data[6],
        mesh->plt_colors.data[7]);
    result.plt_colors2 = glm::uvec4(mesh->plt_colors.data[8], mesh->plt_colors.data[9], 0u, 0u);
    result.emissive_color = glm::vec4(mesh->emissive, 0.0f);
    result.material_params = glm::vec4(mesh->roughness, mesh->specular_strength, 0.0f, 0.0f);
    result.material_texture_indices = material_texture_indices(*mesh);
    return result;
}

static PreparedStaticShadowDrawConstants make_static_shadow_draw_constants(
    ModelRenderContext& render_ctx,
    const PreparedDrawItem& item,
    nw::gfx::BindlessTextureIndex fallback_texture_index)
{
    PreparedStaticShadowDrawConstants result{};
    auto* mesh = item.mesh;
    if (!mesh) {
        return result;
    }

    if (mesh->material_mode == MaterialMode::cutout) {
        ensure_mesh_texture(render_ctx, *mesh);
    }

    result.model = item.world;
    result.texture_flags = glm::uvec4(
        nw::gfx::bindless_texture_index_valid(mesh->texture_index) ? mesh->texture_index : fallback_texture_index,
        mesh->material_mode == MaterialMode::cutout ? 1u : 0u,
        0u,
        0u);
    result.alpha_params = glm::vec4(mesh->alpha_cutout_threshold, 0.0f, 0.0f, 0.0f);
    return result;
}

bool refresh_prepared_static_draw_data(
    ModelRenderContext& render_ctx,
    PreparedStaticDrawDataCache& cache,
    std::span<const PreparedDrawItem* const> draws,
    uint64_t signature)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || draws.empty()
        || draws.size() > std::numeric_limits<uint32_t>::max()) {
        cache.clear();
        return false;
    }

    const uint64_t asset_cache_epoch = render_ctx.assets->epoch();
    if (cache.valid_for(draws.size(), signature, asset_cache_epoch, sizeof(PreparedStaticDrawConstants))) {
        return true;
    }

    for (size_t draw_index = 0; draw_index < draws.size(); ++draw_index) {
        const auto* draw = draws[draw_index];
        if (!draw || !draw->mesh || draw->mesh->is_skin || draw->mesh->material_mode == MaterialMode::water
            || !draw->static_area_geometry.valid() || draw->static_material_index != draw_index) {
            cache.clear();
            return false;
        }
    }

    const auto draw_count = static_cast<uint32_t>(draws.size());
    const uint64_t draw_data_bytes64 = static_cast<uint64_t>(draw_count) * sizeof(PreparedStaticDrawConstants);
    if (draw_data_bytes64 > std::numeric_limits<size_t>::max()
        || draw_data_bytes64 > std::numeric_limits<uint32_t>::max()) {
        cache.clear();
        return false;
    }
    const auto draw_data_bytes = static_cast<size_t>(draw_data_bytes64);

    cache.clear();
    nw::gfx::BufferDesc desc{};
    desc.size = draw_data_bytes;
    desc.usage = nw::gfx::BufferUsage::Storage;
    desc.cpu_visible = true;
    cache.buffer = nw::gfx::create_buffer(render_ctx.gfx, desc);
    if (!cache.buffer.valid()) {
        cache.clear();
        return false;
    }
    auto* constants = static_cast<PreparedStaticDrawConstants*>(nw::gfx::map_buffer(cache.buffer));
    if (!constants) {
        cache.clear();
        return false;
    }

    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());
    for (size_t draw_index = 0; draw_index < draws.size(); ++draw_index) {
        constants[draw_index] = make_static_draw_constants(
            render_ctx, *draws[draw_index], fallback_texture_index);
    }
    nw::gfx::unmap_buffer(cache.buffer);

    cache.span = nw::gfx::StorageSpan{
        cache.buffer,
        0u,
        static_cast<uint32_t>(draw_data_bytes),
    };
    cache.signature = signature;
    cache.asset_cache_epoch = asset_cache_epoch;
    cache.source_draw_count = draw_count;
    return true;
}

bool refresh_prepared_static_shadow_draw_data(
    ModelRenderContext& render_ctx,
    PreparedStaticShadowDrawDataCache& cache,
    std::span<const PreparedDrawItem* const> draws,
    uint64_t signature)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || draws.empty()
        || draws.size() > std::numeric_limits<uint32_t>::max()) {
        cache.clear();
        return false;
    }

    const uint64_t asset_cache_epoch = render_ctx.assets->epoch();
    if (cache.valid_for(draws.size(), signature, asset_cache_epoch, sizeof(PreparedStaticShadowDrawConstants))) {
        return true;
    }

    for (const auto* draw : draws) {
        if (!supports_static_batched_shadow_draw(draw)) {
            cache.clear();
            return false;
        }
    }

    const auto draw_count = static_cast<uint32_t>(draws.size());
    const uint64_t draw_data_bytes64 = static_cast<uint64_t>(draw_count) * sizeof(PreparedStaticShadowDrawConstants);
    if (draw_data_bytes64 > std::numeric_limits<size_t>::max()
        || draw_data_bytes64 > std::numeric_limits<uint32_t>::max()) {
        cache.clear();
        return false;
    }
    const auto draw_data_bytes = static_cast<size_t>(draw_data_bytes64);

    cache.clear();
    nw::gfx::BufferDesc desc{};
    desc.size = draw_data_bytes;
    desc.usage = nw::gfx::BufferUsage::Storage;
    desc.cpu_visible = true;
    cache.buffer = nw::gfx::create_buffer(render_ctx.gfx, desc);
    if (!cache.buffer.valid()) {
        cache.clear();
        return false;
    }
    auto* constants = static_cast<PreparedStaticShadowDrawConstants*>(nw::gfx::map_buffer(cache.buffer));
    if (!constants) {
        cache.clear();
        return false;
    }

    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());
    for (size_t draw_index = 0; draw_index < draws.size(); ++draw_index) {
        constants[draw_index] = make_static_shadow_draw_constants(
            render_ctx, *draws[draw_index], fallback_texture_index);
    }
    nw::gfx::unmap_buffer(cache.buffer);

    cache.span = nw::gfx::StorageSpan{
        cache.buffer,
        0u,
        static_cast<uint32_t>(draw_data_bytes),
    };
    cache.signature = signature;
    cache.asset_cache_epoch = asset_cache_epoch;
    cache.source_draw_count = draw_count;
    return true;
}

static nw::gfx::UniformSpan ensure_static_base_uniform(
    ModelRenderContext& render_ctx,
    PreparedDrawScratch& scratch,
    const nw::render::SceneConstants& base_sc)
{
    if (!scratch.static_base_uniforms.data) {
        scratch.static_base_uniforms = nw::gfx::allocate_uniform_span(
            render_ctx.gfx, sizeof(nw::render::SceneConstants));
        if (scratch.static_base_uniforms.data) {
            std::memcpy(scratch.static_base_uniforms.data, &base_sc, sizeof(base_sc));
        }
    }
    return scratch.static_base_uniforms;
}

struct StaticIndirectSubmit {
    bool submitted = false;
    uint32_t command_count = 0;
    uint32_t instance_count = 0;
    uint32_t max_instances_per_draw = 0;
    uint32_t command_upload_bytes = 0;
    uint32_t cached_command_bytes = 0;
};

static StaticIndirectSubmit submit_precomputed_static_batched_indirect_items(
    ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    const PreparedIndirectDrawCommands& precomputed,
    size_t source_draw_count)
{
    if (!render_ctx.gpu || !precomputed.valid_for(source_draw_count)) {
        return {};
    }

    const size_t command_bytes = precomputed.commands.size() * sizeof(nw::gfx::IndexedIndirectDrawCommand);
    if (command_bytes == 0 || command_bytes > std::numeric_limits<uint32_t>::max()) {
        return {};
    }

    nw::gfx::IndirectDrawSpan commands_span = precomputed.gpu_commands;
    uint32_t command_upload_bytes = 0;
    uint32_t cached_command_bytes = 0;
    if (commands_span.buffer.valid() && commands_span.size >= command_bytes) {
        cached_command_bytes = static_cast<uint32_t>(command_bytes);
    } else {
        const auto mapped_commands = render_ctx.gpu->allocate_indirect_draw_data(
            cmd, static_cast<uint32_t>(command_bytes));
        if (!mapped_commands.span.buffer.valid() || !mapped_commands.data) {
            return {};
        }
        std::memcpy(mapped_commands.data, precomputed.commands.data(), command_bytes);
        commands_span = mapped_commands.span;
        command_upload_bytes = static_cast<uint32_t>(command_bytes);
    }

    nw::gfx::cmd_bind_vertex_buffer(cmd, precomputed.vertices, static_cast<uint32_t>(sizeof(Vertex)));
    nw::gfx::cmd_bind_index_buffer(cmd, precomputed.indices, sizeof(uint16_t));
    nw::gfx::cmd_draw_indexed_indirect(
        cmd,
        commands_span,
        precomputed.command_count,
        sizeof(nw::gfx::IndexedIndirectDrawCommand),
        precomputed.stats);
    return StaticIndirectSubmit{
        .submitted = true,
        .command_count = precomputed.command_count,
        .instance_count = precomputed.instance_count,
        .max_instances_per_draw = precomputed.max_instances_per_draw,
        .command_upload_bytes = command_upload_bytes,
        .cached_command_bytes = cached_command_bytes,
    };
}

static StaticIndirectSubmit submit_static_batched_indirect_items(
    ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draw_items)
{
    if (!render_ctx.gpu || draw_items.empty()
        || draw_items.size() > std::numeric_limits<uint32_t>::max()) {
        return {};
    }

    const auto* first_draw = draw_items.front();
    if (!first_draw || !first_draw->static_area_geometry.valid()) {
        return {};
    }
    const auto& first_geometry = first_draw->static_area_geometry;
    for (const auto* draw : draw_items) {
        if (!draw || !draw->static_area_geometry.valid()
            || draw->static_area_geometry.vertices != first_geometry.vertices
            || draw->static_area_geometry.indices != first_geometry.indices) {
            return {};
        }
    }

    const size_t command_bytes = draw_items.size() * sizeof(nw::gfx::IndexedIndirectDrawCommand);
    if (command_bytes > std::numeric_limits<uint32_t>::max()) {
        return {};
    }

    const auto commands_span = render_ctx.gpu->allocate_indirect_draw_data(
        cmd, static_cast<uint32_t>(command_bytes));
    if (!commands_span.span.buffer.valid() || !commands_span.data) {
        return {};
    }

    auto* commands = static_cast<nw::gfx::IndexedIndirectDrawCommand*>(commands_span.data);
    nw::gfx::IndexedIndirectDrawStats command_stats{};
    uint32_t command_count = 0;
    uint32_t max_instances_per_draw = 0;
    uint32_t draw_index = 0;
    const auto draw_count = static_cast<uint32_t>(draw_items.size());
    while (draw_index < draw_count) {
        const auto* draw = draw_items[draw_index];
        if (!draw) {
            ++draw_index;
            continue;
        }

        uint32_t instance_count = 1;
        while (draw_index + instance_count < draw_count
            && static_batched_indirect_geometry_matches(draw, draw_items[draw_index + instance_count])) {
            ++instance_count;
        }

        const auto& geometry = draw->static_area_geometry;
        commands[command_count++] = nw::gfx::IndexedIndirectDrawCommand{
            .index_count = geometry.index_count,
            .instance_count = instance_count,
            .first_index = geometry.first_index,
            .vertex_offset = geometry.vertex_offset,
            .first_instance = draw_index,
        };
        command_stats.index_count += geometry.index_count;
        command_stats.instance_count += instance_count;
        max_instances_per_draw = std::max(max_instances_per_draw, instance_count);
        draw_index += instance_count;
    }

    if (command_count == 0) {
        return {};
    }

    nw::gfx::cmd_bind_vertex_buffer(cmd, first_geometry.vertices, static_cast<uint32_t>(sizeof(Vertex)));
    nw::gfx::cmd_bind_index_buffer(cmd, first_geometry.indices, sizeof(uint16_t));
    nw::gfx::cmd_draw_indexed_indirect(
        cmd, commands_span.span, command_count, sizeof(nw::gfx::IndexedIndirectDrawCommand), command_stats);
    return StaticIndirectSubmit{
        .submitted = true,
        .command_count = command_count,
        .instance_count = draw_count,
        .max_instances_per_draw = max_instances_per_draw,
        .command_upload_bytes = static_cast<uint32_t>(command_bytes),
    };
}

static bool draw_static_batched_items(
    ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draw_items,
    MaterialMode mode,
    const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index,
    bool offscreen_pass,
    const nw::gfx::UniformSpan& shadow_uniforms,
    const nw::render::ForwardPlusResources* forward_plus,
    PreparedDrawScratch& scratch,
    bool validate_draws = true,
    PreparedIndirectDrawCommands precomputed_indirect = {},
    nw::gfx::StorageSpan cached_draw_data = {},
    bool depth_prepass = false)
{
    if (draw_items.empty()) {
        return true;
    }
    const auto note_failed_batch = [&]() {
        add_saturating(scratch.batch_stats.material_failed_batch_attempt_draw_count, draw_items.size());
    };

    const auto pipeline = static_batched_pipeline_for(render_ctx, mode, offscreen_pass, depth_prepass);
    if (!pipeline.valid()) {
        note_failed_batch();
        return false;
    }

    if (validate_draws) {
        for (const auto* draw : draw_items) {
            if (!draw || !draw->mesh || draw->mesh->is_skin || draw->mesh->material_mode != mode) {
                note_failed_batch();
                return false;
            }
        }
    }

    if (draw_items.size() > std::numeric_limits<uint32_t>::max()) {
        note_failed_batch();
        return false;
    }

    const bool forward_plus_ready = forward_plus_resources_ready(forward_plus);
    if (base_sc.forward_plus_params.x != 0u && !forward_plus_ready) {
        note_failed_batch();
        return false;
    }

    const size_t draw_data_bytes = draw_items.size() * sizeof(PreparedStaticDrawConstants);
    if (draw_data_bytes > std::numeric_limits<uint32_t>::max()) {
        note_failed_batch();
        return false;
    }

    auto uniforms = ensure_static_base_uniform(render_ctx, scratch, base_sc);
    if (!uniforms.data) {
        note_failed_batch();
        return false;
    }

    const auto cached_draw_data_covers_draws = [&]() noexcept {
        if (!cached_draw_data.buffer.valid()) {
            return false;
        }
        bool covered_draw = false;
        for (const auto* draw : draw_items) {
            if (!supports_static_batched_draw(draw, mode) || !draw->static_area_geometry.valid()) {
                continue;
            }
            covered_draw = true;
            if (draw->static_material_index == kInvalidPreparedDrawIndex) {
                return false;
            }
            const uint64_t required_bytes = (static_cast<uint64_t>(draw->static_material_index) + 1u) * sizeof(PreparedStaticDrawConstants);
            if (required_bytes > cached_draw_data.size) {
                return false;
            }
        }
        return covered_draw;
    };
    nw::gfx::StorageSpan draw_data_span = cached_draw_data;
    const bool use_cached_draw_data = cached_draw_data_covers_draws();
    if (use_cached_draw_data) {
        add_saturating(scratch.batch_stats.material_draw_data_cache_hit_count, 1u);
        add_saturating(scratch.batch_stats.material_cached_draw_data_bytes, draw_data_bytes);
    } else {
        const auto draw_data = render_ctx.gpu->allocate_static_draw_data(cmd, static_cast<uint32_t>(draw_data_bytes));
        if (!draw_data.span.buffer.valid() || !draw_data.data) {
            note_failed_batch();
            return false;
        }
        draw_data_span = draw_data.span;
        add_saturating(scratch.batch_stats.material_draw_data_bytes, draw_data_bytes);

        auto* constants = static_cast<PreparedStaticDrawConstants*>(draw_data.data);
        for (size_t draw_index = 0; draw_index < draw_items.size(); ++draw_index) {
            constants[draw_index] = draw_items[draw_index]
                ? make_static_draw_constants(render_ctx, *draw_items[draw_index], fallback_texture_index)
                : PreparedStaticDrawConstants{};
        }
    }

    const nw::gfx::StorageSpan dummy_storage = draw_data_span;
    const nw::gfx::StorageSpan cluster_headers = forward_plus_ready ? forward_plus->cluster_header_buffer : dummy_storage;
    const nw::gfx::StorageSpan cluster_light_indices = forward_plus_ready ? forward_plus->cluster_light_index_buffer : dummy_storage;
    const nw::gfx::StorageSpan lights = forward_plus_ready ? forward_plus->light_buffer : dummy_storage;

    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
        plt_palette_storage(render_ctx), draw_data_span,
        cluster_headers, cluster_light_indices, lights, shadow_uniforms);

    if (precomputed_indirect.source_draw_count != 0) {
        if (const auto indirect = submit_precomputed_static_batched_indirect_items(
                render_ctx, cmd, precomputed_indirect, draw_items.size());
            indirect.submitted) {
            auto& stats = scratch.batch_stats;
            add_saturating(stats.material_batch_count, 1u);
            add_saturating(stats.material_input_draw_count, indirect.instance_count);
            add_saturating(stats.material_instance_count, indirect.instance_count);
            add_saturating(stats.material_draw_call_count, 1u);
            add_saturating(stats.material_indirect_call_count, 1u);
            add_saturating(stats.material_indirect_command_count, indirect.command_count);
            add_saturating(stats.material_indirect_command_upload_bytes, indirect.command_upload_bytes);
            add_saturating(stats.material_cached_indirect_command_bytes, indirect.cached_command_bytes);
            stats.material_max_instances_per_draw = std::max(stats.material_max_instances_per_draw, indirect.max_instances_per_draw);
            return true;
        }
        note_failed_batch();
        return false;
    }

    if (!use_cached_draw_data) {
        if (const auto indirect = submit_static_batched_indirect_items(render_ctx, cmd, draw_items);
            indirect.submitted) {
            auto& stats = scratch.batch_stats;
            add_saturating(stats.material_batch_count, 1u);
            add_saturating(stats.material_input_draw_count, draw_items.size());
            add_saturating(stats.material_instance_count, draw_items.size());
            add_saturating(stats.material_draw_call_count, 1u);
            add_saturating(stats.material_indirect_call_count, 1u);
            add_saturating(stats.material_indirect_command_count, indirect.command_count);
            add_saturating(stats.material_indirect_command_upload_bytes, indirect.command_upload_bytes);
            add_saturating(stats.material_cached_indirect_command_bytes, indirect.cached_command_bytes);
            stats.material_max_instances_per_draw = std::max(stats.material_max_instances_per_draw, indirect.max_instances_per_draw);
            return true;
        }
    }

    uint32_t group_count = 0;
    uint32_t max_instances_per_draw = 0;
    uint32_t draw_index = 0;
    while (draw_index < draw_items.size()) {
        const auto* draw = draw_items[draw_index];
        auto* mesh = draw ? draw->mesh : nullptr;
        if (!mesh) {
            ++draw_index;
            continue;
        }

        const uint32_t first_instance = use_cached_draw_data
            ? draw->static_material_index
            : draw_index;
        uint32_t instance_count = 1;
        while (draw_index + instance_count < draw_items.size()
            && static_batched_geometry_matches(draw, draw_items[draw_index + instance_count])) {
            if (use_cached_draw_data
                && draw_items[draw_index + instance_count]->static_material_index != first_instance + instance_count) {
                break;
            }
            ++instance_count;
        }

        nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(sizeof(Vertex)));
        nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
        nw::gfx::cmd_draw_indexed_base_instance(cmd, mesh->index_count, first_instance, instance_count);
        ++group_count;
        max_instances_per_draw = std::max(max_instances_per_draw, instance_count);
        draw_index += instance_count;
    }
    auto& stats = scratch.batch_stats;
    add_saturating(stats.material_batch_count, 1u);
    add_saturating(stats.material_input_draw_count, draw_items.size());
    add_saturating(stats.material_instance_count, draw_items.size());
    add_saturating(stats.material_draw_call_count, group_count);
    stats.material_max_instances_per_draw = std::max(stats.material_max_instances_per_draw, max_instances_per_draw);
    return true;
}

static bool draw_static_batched_shadow_items(
    ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draw_items,
    const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index,
    PreparedDrawScratch& scratch,
    PreparedIndirectDrawCommands precomputed_indirect = {},
    nw::gfx::StorageSpan cached_draw_data = {})
{
    if (draw_items.empty()) {
        return true;
    }
    const auto note_failed_batch = [&]() {
        add_saturating(scratch.batch_stats.shadow_failed_batch_attempt_draw_count, draw_items.size());
    };

    const auto pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_batched, MaterialMode::opaque, false, ModelPipelinePass::shadow);
    if (!pipeline.valid()) {
        note_failed_batch();
        return false;
    }

    for (const auto* draw : draw_items) {
        if (!supports_static_batched_shadow_draw(draw)) {
            note_failed_batch();
            return false;
        }
    }

    if (draw_items.size() > std::numeric_limits<uint32_t>::max()) {
        note_failed_batch();
        return false;
    }
    const size_t draw_data_bytes = draw_items.size() * sizeof(PreparedStaticShadowDrawConstants);
    if (draw_data_bytes > std::numeric_limits<uint32_t>::max()) {
        note_failed_batch();
        return false;
    }

    auto uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(nw::render::SceneConstants));
    if (!uniforms.data) {
        note_failed_batch();
        return false;
    }
    std::memcpy(uniforms.data, &base_sc, sizeof(base_sc));

    nw::gfx::StorageSpan draw_data_span = cached_draw_data;
    const bool use_cached_draw_data = draw_data_span.buffer.valid() && static_cast<uint64_t>(draw_data_span.size) >= draw_data_bytes;
    if (use_cached_draw_data) {
        add_saturating(scratch.batch_stats.shadow_draw_data_cache_hit_count, 1u);
        add_saturating(scratch.batch_stats.shadow_cached_draw_data_bytes, draw_data_bytes);
    } else {
        const auto draw_data = render_ctx.gpu->allocate_static_draw_data(cmd, static_cast<uint32_t>(draw_data_bytes));
        if (!draw_data.span.buffer.valid() || !draw_data.data) {
            note_failed_batch();
            return false;
        }
        draw_data_span = draw_data.span;
        add_saturating(scratch.batch_stats.shadow_draw_data_bytes, draw_data_bytes);

        auto* constants = static_cast<PreparedStaticShadowDrawConstants*>(draw_data.data);
        for (size_t draw_index = 0; draw_index < draw_items.size(); ++draw_index) {
            constants[draw_index] = make_static_shadow_draw_constants(
                render_ctx, *draw_items[draw_index], fallback_texture_index);
        }
    }

    nw::gfx::cmd_bind_pipeline(cmd, pipeline);
    nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms,
        plt_palette_storage(render_ctx), draw_data_span);

    if (precomputed_indirect.source_draw_count != 0) {
        if (const auto indirect = submit_precomputed_static_batched_indirect_items(
                render_ctx, cmd, precomputed_indirect, draw_items.size());
            indirect.submitted) {
            auto& stats = scratch.batch_stats;
            add_saturating(stats.shadow_batch_count, 1u);
            add_saturating(stats.shadow_input_draw_count, indirect.instance_count);
            add_saturating(stats.shadow_instance_count, indirect.instance_count);
            add_saturating(stats.shadow_draw_call_count, 1u);
            add_saturating(stats.shadow_indirect_call_count, 1u);
            add_saturating(stats.shadow_indirect_command_count, indirect.command_count);
            add_saturating(stats.shadow_indirect_command_upload_bytes, indirect.command_upload_bytes);
            add_saturating(stats.shadow_cached_indirect_command_bytes, indirect.cached_command_bytes);
            stats.shadow_max_instances_per_draw = std::max(stats.shadow_max_instances_per_draw, indirect.max_instances_per_draw);
            return true;
        }
        note_failed_batch();
        return false;
    }

    if (const auto indirect = submit_static_batched_indirect_items(render_ctx, cmd, draw_items);
        indirect.submitted) {
        auto& stats = scratch.batch_stats;
        add_saturating(stats.shadow_batch_count, 1u);
        add_saturating(stats.shadow_input_draw_count, draw_items.size());
        add_saturating(stats.shadow_instance_count, draw_items.size());
        add_saturating(stats.shadow_draw_call_count, 1u);
        add_saturating(stats.shadow_indirect_call_count, 1u);
        add_saturating(stats.shadow_indirect_command_count, indirect.command_count);
        add_saturating(stats.shadow_indirect_command_upload_bytes, indirect.command_upload_bytes);
        add_saturating(stats.shadow_cached_indirect_command_bytes, indirect.cached_command_bytes);
        stats.shadow_max_instances_per_draw = std::max(stats.shadow_max_instances_per_draw, indirect.max_instances_per_draw);
        return true;
    }

    uint32_t group_count = 0;
    uint32_t max_instances_per_draw = 0;
    uint32_t draw_index = 0;
    while (draw_index < draw_items.size()) {
        const auto* draw = draw_items[draw_index];
        auto* mesh = draw ? draw->mesh : nullptr;
        if (!mesh) {
            ++draw_index;
            continue;
        }

        uint32_t instance_count = 1;
        while (draw_index + instance_count < draw_items.size()
            && static_batched_geometry_matches(draw, draw_items[draw_index + instance_count])) {
            ++instance_count;
        }

        nw::gfx::cmd_bind_vertex_buffer(cmd, mesh->vertices, static_cast<uint32_t>(sizeof(Vertex)));
        nw::gfx::cmd_bind_index_buffer(cmd, mesh->indices, sizeof(uint16_t));
        nw::gfx::cmd_draw_indexed_base_instance(cmd, mesh->index_count, draw_index, instance_count);
        ++group_count;
        max_instances_per_draw = std::max(max_instances_per_draw, instance_count);
        draw_index += instance_count;
    }
    auto& stats = scratch.batch_stats;
    add_saturating(stats.shadow_batch_count, 1u);
    add_saturating(stats.shadow_input_draw_count, draw_items.size());
    add_saturating(stats.shadow_instance_count, draw_items.size());
    add_saturating(stats.shadow_draw_call_count, group_count);
    stats.shadow_max_instances_per_draw = std::max(stats.shadow_max_instances_per_draw, max_instances_per_draw);
    return true;
}

static void draw_material_pointer_items(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draw_items, MaterialMode mode, const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index, const glm::vec3& camera_position,
    bool offscreen_pass, const nw::gfx::UniformSpan& shadow_uniforms,
    PreparedDrawScratch& scratch,
    const nw::render::ForwardPlusResources* forward_plus = nullptr,
    bool static_geometry_sorted = false,
    PreparedIndirectDrawCommands precomputed_indirect = {},
    nw::gfx::StorageSpan cached_static_draw_data = {})
{
    auto& batched_draws = scratch.batched_draws;
    auto& fallback_draws = scratch.fallback_draws;
    const auto draw_partitioned = [&](std::span<const PreparedDrawItem* const> source_draws) {
        batched_draws.clear();
        fallback_draws.clear();
        batched_draws.reserve(source_draws.size());
        fallback_draws.reserve(source_draws.size());
        for (const auto* draw : source_draws) {
            if (supports_static_batched_draw(draw, mode)) {
                batched_draws.push_back(draw);
            } else if (draw && draw->mesh) {
                fallback_draws.push_back(draw);
            }
        }
    };
    const auto draw_fallback = [&](std::span<const PreparedDrawItem* const> fallback_items) {
        add_saturating(scratch.batch_stats.material_fallback_draw_count, fallback_items.size());
        auto scene_uniforms = allocate_scene_uniform_batch(render_ctx.gfx, fallback_items.size());
        for (const auto* draw : fallback_items) {
            draw_mesh_item(render_ctx, cmd, *draw, base_sc, fallback_texture_index, offscreen_pass, shadow_uniforms,
                &scene_uniforms, forward_plus);
        }
    };

    if (static_geometry_sorted && mode != MaterialMode::transparent) {
        if (precomputed_indirect.valid_for(draw_items.size())) {
            fallback_draws.clear();
            fallback_draws.reserve(draw_items.size());
            for (const auto* draw : draw_items) {
                if (!supports_static_batched_draw(draw, mode) && draw && draw->mesh) {
                    fallback_draws.push_back(draw);
                }
            }
            if (draw_static_batched_items(render_ctx, cmd, draw_items, mode, base_sc, fallback_texture_index,
                    offscreen_pass, shadow_uniforms, forward_plus, scratch, false,
                    precomputed_indirect, cached_static_draw_data)) {
                if (!fallback_draws.empty()) {
                    draw_fallback(fallback_draws);
                }
                return;
            }
        }
        if (draw_items.size() < kPartitionSortedStaticBatchThreshold
            && draw_static_batched_items(render_ctx, cmd, draw_items, mode, base_sc, fallback_texture_index,
                offscreen_pass, shadow_uniforms, forward_plus, scratch)) {
            return;
        }
        draw_partitioned(draw_items);
        if (!batched_draws.empty()
            && !draw_static_batched_items(render_ctx, cmd, batched_draws, mode, base_sc, fallback_texture_index,
                offscreen_pass, shadow_uniforms, forward_plus, scratch, false)) {
            fallback_draws.insert(fallback_draws.end(), batched_draws.begin(), batched_draws.end());
        }
        if (!fallback_draws.empty()) {
            draw_fallback(fallback_draws);
        }
        return;
    }

    if (mode == MaterialMode::transparent) {
        auto& sorted_draws = scratch.material_draws;
        if (draw_items.data() != sorted_draws.data()) {
            sorted_draws.clear();
            sorted_draws.reserve(draw_items.size());
            for (const auto* draw : draw_items) {
                sorted_draws.push_back(draw);
            }
        }
        std::sort(sorted_draws.begin(), sorted_draws.end(),
            [&](const PreparedDrawItem* lhs, const PreparedDrawItem* rhs) {
                const int lhs_hint = lhs->mesh ? lhs->mesh->transparencyhint : 0;
                const int rhs_hint = rhs->mesh ? rhs->mesh->transparencyhint : 0;
                if (lhs_hint != rhs_hint) {
                    return lhs_hint < rhs_hint;
                }
                const bool lhs_ground = lhs->mesh && lhs->mesh->is_ground_overlay;
                const bool rhs_ground = rhs->mesh && rhs->mesh->is_ground_overlay;
                if (lhs_ground != rhs_ground) {
                    return lhs_ground;
                }
                if (lhs_ground && rhs_ground) {
                    return lhs->order < rhs->order;
                }
                const auto lhs_delta = lhs->sort_position - camera_position;
                const auto rhs_delta = rhs->sort_position - camera_position;
                const float lhs_dist2 = glm::dot(lhs_delta, lhs_delta);
                const float rhs_dist2 = glm::dot(rhs_delta, rhs_delta);
                if (lhs_dist2 != rhs_dist2) {
                    return lhs_dist2 > rhs_dist2;
                }
                return lhs->order < rhs->order;
            });
        draw_partitioned(sorted_draws);
        if (fallback_draws.empty() && draw_static_batched_items(render_ctx, cmd, batched_draws, mode, base_sc, fallback_texture_index, offscreen_pass, shadow_uniforms, forward_plus, scratch, false)) {
            return;
        }
        draw_fallback(sorted_draws);
        return;
    }

    draw_partitioned(draw_items);
    std::sort(batched_draws.begin(), batched_draws.end(), static_batched_geometry_less);
    if (!batched_draws.empty()
        && !draw_static_batched_items(render_ctx, cmd, batched_draws, mode, base_sc, fallback_texture_index,
            offscreen_pass, shadow_uniforms, forward_plus, scratch, false)) {
        fallback_draws.insert(fallback_draws.end(), batched_draws.begin(), batched_draws.end());
    }
    if (!fallback_draws.empty()) {
        draw_fallback(fallback_draws);
    }
}

static void draw_material_pointer_pass(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws, MaterialMode mode, const nw::render::SceneConstants& base_sc,
    nw::gfx::BindlessTextureIndex fallback_texture_index, const glm::vec3& camera_position,
    bool offscreen_pass, const nw::gfx::UniformSpan& shadow_uniforms,
    PreparedDrawScratch& scratch,
    const nw::render::ForwardPlusResources* forward_plus = nullptr)
{
    auto& draw_pointers = scratch.material_draws;
    draw_pointers.clear();
    draw_pointers.reserve(draws.size());
    for (const auto* draw : draws) {
        if (draw && draw->mesh && draw->mesh->material_mode == mode) {
            draw_pointers.push_back(draw);
        }
    }
    draw_material_pointer_items(render_ctx, cmd, draw_pointers, mode, base_sc, fallback_texture_index,
        camera_position, offscreen_pass, shadow_uniforms, scratch, forward_plus);
}

static nw::render::SceneConstants make_base_scene_constants(const nw::render::RenderContext& ctx)
{
    auto base_sc = nw::render::make_scene_constants(ctx);
    base_sc.pad_alpha.y = ctx.time_seconds;
    return base_sc;
}

static nw::gfx::UniformSpan upload_shadow_uniforms(
    ModelRenderContext& render_ctx,
    const nw::render::RenderContext& ctx)
{
    auto shadow_uniforms = nw::gfx::allocate_uniform_span(render_ctx.gfx, sizeof(nw::render::ShadowConstants));
    if (shadow_uniforms.data) {
        nw::render::ShadowConstants shadow_constants{};
        if (ctx.shadow.enabled) {
            shadow_constants.world_to_shadow = ctx.shadow.world_to_shadow;
            shadow_constants.shadow_split_distances = glm::vec4(
                ctx.shadow.split_distances[0],
                ctx.shadow.split_distances[1],
                ctx.shadow.split_distances[2],
                0.0f);
            for (size_t i = 0; i < nw::render::kShadowCascadeCount; ++i) {
                shadow_constants.shadow_texture_indices[static_cast<glm::length_t>(i)] = nw::gfx::get_bindless_texture_index(render_ctx.gfx, ctx.shadow.depth_textures[i]);
            }
            shadow_constants.shadow_strength = ctx.shadow.strength;
            shadow_constants.debug_mode = ctx.shadow.debug_mode;
        }
        const auto& local_shadows = ctx.local_shadows;
        if (local_shadows.count > 0) {
            const uint32_t count = std::min(local_shadows.count, nw::render::kLocalShadowCount);
            for (uint32_t i = 0; i < count; ++i) {
                shadow_constants.local_world_to_shadow[i] = local_shadows.world_to_shadow[i];
                const auto texture_index = nw::gfx::get_bindless_texture_index(render_ctx.gfx, local_shadows.depth_textures[i]);
                shadow_constants.local_shadow_texture_indices[static_cast<glm::length_t>(i)] = texture_index;
            }
            // z/w are reserved; local maps currently sample unoffset receivers.
            shadow_constants.local_shadow_params = glm::vec4(
                local_shadows.strength, static_cast<float>(count), 0.0f, 0.0f);
        }
        std::memcpy(shadow_uniforms.data, &shadow_constants, sizeof(shadow_constants));
    }
    return shadow_uniforms;
}

static std::span<const PreparedDrawItem* const> prepared_draw_pointer_span(
    std::span<const PreparedDrawItem> draws,
    PreparedDrawScratch& scratch)
{
    auto& draw_pointers = scratch.transparent_draws;
    draw_pointers.clear();
    draw_pointers.reserve(draws.size());
    for (const auto& draw : draws) {
        if (draw.mesh) {
            draw_pointers.push_back(&draw);
        }
    }
    return std::span<const PreparedDrawItem* const>{draw_pointers.data(), draw_pointers.size()};
}

static std::span<const PreparedDrawItem* const> prepared_draw_pointer_span(
    std::span<const PreparedDrawItem* const> draws,
    PreparedDrawScratch&)
{
    return draws;
}

static void render_prepared_model_pointer_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    nw::render::RenderPassSelection pass)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || !render_ctx.legacy_gpu || !cmd || draws.empty()) {
        return;
    }

    nw::render::SceneConstants base_sc = make_base_scene_constants(ctx);
    if (!scratch.shadow_uniforms.data) {
        scratch.shadow_uniforms = upload_shadow_uniforms(render_ctx, ctx);
    }
    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());

    const auto draw_pass = [&](MaterialMode mode) {
        draw_material_pointer_pass(render_ctx, cmd, draws, mode, base_sc, fallback_texture_index,
            ctx.camera_position, ctx.offscreen_pass, scratch.shadow_uniforms, scratch, &ctx.forward_plus);
    };

    if (pass == nw::render::RenderPassSelection::opaque_cutout || pass == nw::render::RenderPassSelection::all) {
        draw_pass(MaterialMode::opaque);
        draw_pass(MaterialMode::cutout);
    }
    if (pass == nw::render::RenderPassSelection::water || pass == nw::render::RenderPassSelection::all) {
        draw_pass(MaterialMode::water);
    }
    if (pass == nw::render::RenderPassSelection::transparent || pass == nw::render::RenderPassSelection::all) {
        draw_pass(MaterialMode::transparent);
    }
}

void render_prepared_model_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem> draws,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass)
{
    PreparedDrawScratch scratch;
    render_prepared_model_draws(render_ctx, cmd, draws, ctx, scratch, pass);
}

void render_prepared_model_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem> draws,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    nw::render::RenderPassSelection pass)
{
    render_prepared_model_pointer_draws(
        render_ctx, cmd, prepared_draw_pointer_span(draws, scratch), ctx, scratch, pass);
}

void render_prepared_model_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    nw::render::RenderPassSelection pass)
{
    render_prepared_model_pointer_draws(
        render_ctx, cmd, prepared_draw_pointer_span(draws, scratch), ctx, scratch, pass);
}

void render_prepared_material_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    bool static_geometry_sorted,
    PreparedIndirectDrawCommands precomputed_indirect,
    nw::gfx::StorageSpan cached_static_draw_data)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || !render_ctx.legacy_gpu || !cmd || draws.empty()) {
        return;
    }

    nw::render::SceneConstants base_sc = make_base_scene_constants(ctx);
    if (!scratch.shadow_uniforms.data) {
        scratch.shadow_uniforms = upload_shadow_uniforms(render_ctx, ctx);
    }
    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());
    draw_material_pointer_items(render_ctx, cmd, draws, mode, base_sc, fallback_texture_index,
        ctx.camera_position, ctx.offscreen_pass, scratch.shadow_uniforms, scratch, &ctx.forward_plus,
        static_geometry_sorted, precomputed_indirect,
        cached_static_draw_data);
}

bool render_prepared_material_depth_prepass(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    bool static_geometry_sorted,
    PreparedIndirectDrawCommands precomputed_indirect,
    nw::gfx::StorageSpan cached_static_draw_data)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || !render_ctx.legacy_gpu || !cmd || draws.empty()) {
        return false;
    }
    const auto depth_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_batched, mode, false, ModelPipelinePass::depth_prepass);
    if (!depth_pipeline.valid()) {
        return false;
    }

    // Partition out the batchable static draws (mirrors draw_material_pointer_items). Skinned
    // and other fallback draws are left to the color pass; the depth pre-pass only covers the
    // indirect-batched static geometry, which is where the overdraw win is.
    auto& batched = scratch.batched_draws;
    batched.clear();
    batched.reserve(draws.size());
    for (const auto* draw : draws) {
        if (supports_static_batched_draw(draw, mode)) {
            batched.push_back(draw);
        }
    }
    if (batched.empty()) {
        return false;
    }
    if (!static_geometry_sorted) {
        std::sort(batched.begin(), batched.end(), static_batched_geometry_less);
    }

    nw::render::SceneConstants base_sc = make_base_scene_constants(ctx);
    if (!scratch.shadow_uniforms.data) {
        scratch.shadow_uniforms = upload_shadow_uniforms(render_ctx, ctx);
    }
    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());

    // Build indirect commands inline (empty precomputed + empty cached draw data) so the
    // pre-pass issues a single indirect draw per geometry buffer, matching the color path's
    // inline-batched submission. Passing the cached draw data here would instead force the
    // slow per-group base-instance loop. (precomputed_indirect/cached_static_draw_data are
    // accepted for symmetry with the color entry point and a future persistent-buffer path.)
    (void)precomputed_indirect;
    (void)cached_static_draw_data;
    return draw_static_batched_items(render_ctx, cmd, batched, mode, base_sc, fallback_texture_index,
        ctx.offscreen_pass, scratch.shadow_uniforms, &ctx.forward_plus, scratch, /*validate_draws=*/false,
        /*precomputed_indirect=*/{}, /*cached_draw_data=*/{}, /*depth_prepass=*/true);
}

bool render_prepared_shadow_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    const glm::mat4& light_view,
    const glm::mat4& light_projection,
    PreparedDrawScratch& scratch,
    bool static_geometry_sorted,
    PreparedIndirectDrawCommands precomputed_indirect,
    nw::gfx::StorageSpan cached_shadow_draw_data)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || !render_ctx.legacy_gpu || !cmd || draws.empty()) {
        return false;
    }

    nw::render::SceneConstants base_sc{};
    base_sc.view = light_view;
    base_sc.projection = light_projection;

    auto& shadow_draws = scratch.batched_draws;
    shadow_draws.clear();
    shadow_draws.reserve(draws.size());
    for (const auto* draw : draws) {
        if (!draw || !draw->mesh || is_translucent_material(draw->mesh->material_mode)) {
            continue;
        }
        if (draw->mesh->is_skin) {
            return false;
        }
        shadow_draws.push_back(draw);
    }
    if (shadow_draws.empty()) {
        return true;
    }
    if (!static_geometry_sorted) {
        std::sort(shadow_draws.begin(), shadow_draws.end(), static_batched_geometry_less);
    }

    PreparedIndirectDrawCommands shadow_indirect;
    if (static_geometry_sorted && precomputed_indirect.valid_for(shadow_draws.size())) {
        shadow_indirect = precomputed_indirect;
    }
    return draw_static_batched_shadow_items(render_ctx, cmd, shadow_draws, base_sc,
        nw::gfx::get_bindless_texture_index(render_ctx.gfx, render_ctx.gpu->fallback_texture()), scratch,
        shadow_indirect, cached_shadow_draw_data);
}

void render_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const nw::render::RenderContext& ctx, nw::render::RenderPassSelection pass)
{
    render_model_instance_with_root(render_ctx, cmd, model, model.root_transform(), ctx, pass);
}

void render_model_instance_with_root(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const ModelInstance& model, const glm::mat4& model_root, const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass)
{
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.assets || !render_ctx.legacy_gpu || !cmd
        || !model.render_enabled || model.nodes_.empty()) {
        return;
    }
    if (!model_supports_pass(model, pass)) {
        return;
    }

    std::vector<PreparedDrawItem> draws;
    collect_prepared_draws(draws, model, model_root);
    render_prepared_model_draws(render_ctx, cmd, draws, ctx, pass);
}

void render_shadow_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd,
    const ModelInstance& model, const glm::mat4& model_root, const glm::mat4& light_view,
    const glm::mat4& light_projection)
{
    const auto shadow_pipeline = model_pipeline_for(render_ctx, ModelPipelineMeshKind::static_mesh, MaterialMode::opaque, false, ModelPipelinePass::shadow);
    if (!render_ctx.gfx || !render_ctx.gpu || !render_ctx.legacy_gpu || !shadow_pipeline.valid() || !cmd
        || !model.render_enabled
        || model.nodes_.empty()) {
        return;
    }

    nw::render::SceneConstants base_sc{};
    base_sc.view = light_view;
    base_sc.projection = light_projection;

    const auto fallback_texture_index = nw::gfx::get_bindless_texture_index(
        render_ctx.gfx, render_ctx.gpu->fallback_texture());
    auto scene_uniforms = allocate_scene_uniform_batch(render_ctx.gfx, model.shadow_casters().size());
    for (Mesh* mesh : model.shadow_casters()) {
        draw_shadow_cached_mesh(render_ctx, cmd, mesh, model_root, base_sc, fallback_texture_index, &scene_uniforms);
    }
}

} // namespace nw::render::nwn
