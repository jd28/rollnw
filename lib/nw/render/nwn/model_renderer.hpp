#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/render/model_draw.hpp>
#include <nw/render/nwn/model_render_context.hpp>
#include <nw/render/render_context.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace nw::render::nwn {

struct Mesh;
struct ModelInstance;
class RenderAssetCache;

inline constexpr uint32_t kInvalidPreparedDrawIndex = std::numeric_limits<uint32_t>::max();

struct PreparedDrawGeometry {
    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return vertices.valid() && indices.valid() && index_count > 0;
    }
};

struct PreparedDrawItem {
    Mesh* mesh = nullptr;
    glm::mat4 world{1.0f};
    glm::mat4 normal_matrix{1.0f};
    glm::mat4 model_root{1.0f};
    glm::vec3 sort_position{0.0f};
    nw::render::Bounds light_bounds{};
    PreparedDrawGeometry static_area_geometry{};
    size_t order = 0;
    uint32_t light_index_offset = 0;
    uint32_t light_index_count = 0;
    uint32_t static_material_index = kInvalidPreparedDrawIndex;
};

struct PreparedStaticDrawConstants {
    glm::mat4 model{1.0f};
    glm::mat4 normal_matrix{1.0f};
    glm::vec4 color_key_threshold{0.0f};
    glm::vec4 pad_alpha{0.0f};
    glm::uvec4 texture_flags{0u};
    glm::vec4 alpha_params{0.0f};
    glm::uvec4 plt_colors0{0u};
    glm::uvec4 plt_colors1{0u};
    glm::uvec4 plt_colors2{0u};
    glm::vec4 emissive_color{0.0f};
    glm::vec4 material_params{0.78f, 0.12f, 0.0f, 0.0f};
    glm::uvec4 material_texture_indices{0u};
};

struct PreparedStaticShadowDrawConstants {
    glm::mat4 model{1.0f};
    glm::uvec4 texture_flags{0u};
    glm::vec4 alpha_params{0.0f};
};

struct PreparedDrawDataCache {
    nw::gfx::Handle<nw::gfx::Buffer> buffer;
    nw::gfx::StorageSpan span;
    uint64_t signature = 0;
    uint64_t asset_cache_epoch = 0;
    uint32_t source_draw_count = std::numeric_limits<uint32_t>::max();

    PreparedDrawDataCache() = default;
    ~PreparedDrawDataCache();
    PreparedDrawDataCache(const PreparedDrawDataCache&) = delete;
    PreparedDrawDataCache& operator=(const PreparedDrawDataCache&) = delete;
    PreparedDrawDataCache(PreparedDrawDataCache&& other) noexcept;
    PreparedDrawDataCache& operator=(PreparedDrawDataCache&& other) noexcept;

    void clear();
    [[nodiscard]] bool valid_for(
        size_t input_draw_count,
        uint64_t input_signature,
        uint64_t input_asset_cache_epoch,
        size_t bytes_per_draw) const noexcept;
};

using PreparedStaticDrawDataCache = PreparedDrawDataCache;
using PreparedStaticShadowDrawDataCache = PreparedDrawDataCache;

struct PreparedIndirectDrawCommands {
    std::span<const nw::gfx::IndexedIndirectDrawCommand> commands;
    nw::gfx::IndirectDrawSpan gpu_commands;
    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    nw::gfx::IndexedIndirectDrawStats stats{};
    uint32_t source_draw_count = 0;
    uint32_t instance_count = 0;
    uint32_t command_count = 0;
    uint32_t max_instances_per_draw = 0;

    [[nodiscard]] bool valid_for(size_t input_draw_count) const noexcept
    {
        return !commands.empty()
            && commands.size() == command_count
            && vertices.valid()
            && indices.valid()
            && source_draw_count == input_draw_count
            && input_draw_count <= std::numeric_limits<uint32_t>::max();
    }
};

struct PreparedDrawBatchStats {
    uint32_t material_batch_count = 0;
    uint32_t material_input_draw_count = 0;
    uint32_t material_instance_count = 0;
    uint32_t material_draw_call_count = 0;
    uint32_t material_fallback_draw_count = 0;
    uint32_t material_failed_batch_attempt_draw_count = 0;
    uint32_t material_indirect_call_count = 0;
    uint32_t material_indirect_command_count = 0;
    uint32_t material_max_instances_per_draw = 0;
    uint64_t material_indirect_command_upload_bytes = 0;
    uint64_t material_cached_indirect_command_bytes = 0;
    uint64_t material_draw_data_bytes = 0;
    uint32_t material_draw_data_cache_hit_count = 0;
    uint64_t material_cached_draw_data_bytes = 0;
    uint32_t shadow_batch_count = 0;
    uint32_t shadow_input_draw_count = 0;
    uint32_t shadow_instance_count = 0;
    uint32_t shadow_draw_call_count = 0;
    uint32_t shadow_failed_batch_attempt_draw_count = 0;
    uint32_t shadow_indirect_call_count = 0;
    uint32_t shadow_indirect_command_count = 0;
    uint32_t shadow_max_instances_per_draw = 0;
    uint64_t shadow_indirect_command_upload_bytes = 0;
    uint64_t shadow_cached_indirect_command_bytes = 0;
    uint64_t shadow_draw_data_bytes = 0;
    uint32_t shadow_draw_data_cache_hit_count = 0;
    uint64_t shadow_cached_draw_data_bytes = 0;
};

struct PreparedDrawScratch {
    std::vector<const PreparedDrawItem*> material_draws;
    std::vector<const PreparedDrawItem*> batched_draws;
    std::vector<const PreparedDrawItem*> fallback_draws;
    std::vector<const PreparedDrawItem*> transparent_draws;
    std::vector<const PreparedDrawItem*> bridge_draws;
    nw::gfx::UniformSpan shadow_uniforms;
    nw::gfx::UniformSpan static_base_uniforms;
    PreparedDrawBatchStats batch_stats{};

    void clear()
    {
        material_draws.clear();
        batched_draws.clear();
        fallback_draws.clear();
        transparent_draws.clear();
        bridge_draws.clear();
        shadow_uniforms = {};
        static_base_uniforms = {};
        batch_stats = {};
    }

    void begin_frame()
    {
        shadow_uniforms = {};
        static_base_uniforms = {};
        batch_stats = {};
        material_draws.clear();
        batched_draws.clear();
        fallback_draws.clear();
        transparent_draws.clear();
        bridge_draws.clear();
    }

    void reserve(size_t draw_count)
    {
        material_draws.reserve(draw_count);
        batched_draws.reserve(draw_count);
        fallback_draws.reserve(draw_count);
        transparent_draws.reserve(draw_count);
        bridge_draws.reserve(draw_count);
    }
};

void collect_prepared_draws(
    std::vector<PreparedDrawItem>& draws,
    const ModelInstance& model,
    const glm::mat4& model_root);
void collect_local_light_indices_for_bounds(
    std::vector<uint32_t>& indices,
    std::span<const nw::render::LocalLight> lights,
    const nw::render::Bounds& bounds);
bool build_prepared_indirect_draw_commands(
    std::vector<nw::gfx::IndexedIndirectDrawCommand>& command_storage,
    PreparedIndirectDrawCommands& out,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode);
bool build_prepared_static_material_indirect_draw_commands(
    std::vector<nw::gfx::IndexedIndirectDrawCommand>& command_storage,
    PreparedIndirectDrawCommands& out,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode);
bool build_prepared_shadow_indirect_draw_commands(
    std::vector<nw::gfx::IndexedIndirectDrawCommand>& command_storage,
    PreparedIndirectDrawCommands& out,
    std::span<const PreparedDrawItem* const> draws);
void render_prepared_model_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem> draws,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass = nw::render::RenderPassSelection::all);
void render_prepared_model_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem> draws,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    nw::render::RenderPassSelection pass = nw::render::RenderPassSelection::all);
void render_prepared_model_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    nw::render::RenderPassSelection pass = nw::render::RenderPassSelection::all);
void render_prepared_material_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    bool static_geometry_sorted = false,
    PreparedIndirectDrawCommands precomputed_indirect = {},
    nw::gfx::StorageSpan cached_static_draw_data = {});
// Depth-only pre-pass over the opaque static draws: same geometry/transforms as the
// color pass, color writes disabled, so the subsequent LESS_EQUAL color pass gets early-Z
// rejection of occluded fragments. Returns false (and draws nothing) when the sorted-static
// fast path with a precomputed indirect buffer is unavailable.
bool render_prepared_material_depth_prepass(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    nw::render::MaterialMode mode,
    const nw::render::RenderContext& ctx,
    PreparedDrawScratch& scratch,
    bool static_geometry_sorted = false,
    PreparedIndirectDrawCommands precomputed_indirect = {},
    nw::gfx::StorageSpan cached_static_draw_data = {});
bool refresh_prepared_static_draw_data(
    ModelRenderContext& render_ctx,
    PreparedStaticDrawDataCache& cache,
    std::span<const PreparedDrawItem* const> draws,
    uint64_t signature);
bool render_prepared_shadow_draws(ModelRenderContext& render_ctx,
    nw::gfx::CommandList* cmd,
    std::span<const PreparedDrawItem* const> draws,
    const glm::mat4& light_view,
    const glm::mat4& light_projection,
    PreparedDrawScratch& scratch,
    bool static_geometry_sorted = false,
    PreparedIndirectDrawCommands precomputed_indirect = {},
    nw::gfx::StorageSpan cached_shadow_draw_data = {});
bool refresh_prepared_static_shadow_draw_data(
    ModelRenderContext& render_ctx,
    PreparedStaticShadowDrawDataCache& cache,
    std::span<const PreparedDrawItem* const> draws,
    uint64_t signature);
void render_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass = nw::render::RenderPassSelection::all);
void render_model_instance_with_root(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const glm::mat4& model_root, const nw::render::RenderContext& ctx,
    nw::render::RenderPassSelection pass = nw::render::RenderPassSelection::all);
void render_shadow_model_instance(ModelRenderContext& render_ctx, nw::gfx::CommandList* cmd, const ModelInstance& model,
    const glm::mat4& model_root, const glm::mat4& light_view, const glm::mat4& light_projection);

} // namespace nw::render::nwn
