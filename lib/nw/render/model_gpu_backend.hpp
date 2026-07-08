#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/render/frame_storage_arena.hpp>
#include <nw/render/model.hpp>
#include <nw/render/pipeline_cache.hpp>
#include <nw/render/shader_provider.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace nw::render {

struct MappedIndirectDrawSpan {
    nw::gfx::IndirectDrawSpan span;
    void* data = nullptr;
};

enum class ModelPipelineMeshKind : uint8_t {
    static_mesh,
    static_batched,
    skinned,
    pbr_static,
    pbr_skinned,
};

enum class ModelPipelinePass : uint8_t {
    color,
    depth_prepass,
    shadow,
};

enum class ModelPipelineTarget : uint8_t {
    onscreen,
    offscreen,
};

struct ModelPipelineKey {
    ModelPipelineMeshKind mesh = ModelPipelineMeshKind::static_mesh;
    MaterialMode material = MaterialMode::opaque;
    ModelPipelineTarget target = ModelPipelineTarget::onscreen;
    ModelPipelinePass pass = ModelPipelinePass::color;
};

class ModelGpuBackend {
    enum class PipelineSlot : uint8_t {
        static_mesh,
        static_batched,
        static_batched_depth,
        static_shadow,
        static_shadow_cutout,
        static_batched_shadow,
        static_offscreen,
        static_batched_offscreen,
        static_offscreen_transparent,
        static_batched_offscreen_transparent,
        static_offscreen_water,
        static_transparent,
        static_batched_transparent,
        static_water,
        skinned,
        skinned_shadow,
        skinned_shadow_cutout,
        skinned_offscreen,
        skinned_offscreen_transparent,
        skinned_transparent,
        pbr_static_opaque,
        pbr_static_cutout,
        pbr_static_transparent,
        pbr_static_shadow,
        pbr_static_shadow_cutout,
        pbr_skinned_opaque,
        pbr_skinned_cutout,
        pbr_skinned_transparent,
        pbr_skinned_shadow,
        pbr_skinned_shadow_cutout,
        count,
    };

public:
    explicit ModelGpuBackend(nw::gfx::Context* ctx)
        : ctx_{ctx}
        , pipeline_cache_{ctx}
    {
    }

    ~ModelGpuBackend();

    bool initialize(nw::render::ShaderProvider& shader_provider);
    nw::gfx::StorageSpan upload_bones(nw::gfx::CommandList* cmd, const glm::mat4* bones, uint32_t count);
    nw::gfx::StorageSpan upload_frame_storage(
        nw::gfx::CommandList* cmd, const void* data, uint32_t size, uint32_t alignment = 64);
    MappedStorageSpan allocate_static_draw_data(nw::gfx::CommandList* cmd, uint32_t size);
    MappedIndirectDrawSpan allocate_indirect_draw_data(nw::gfx::CommandList* cmd, uint32_t size);
    nw::gfx::StorageSpan upload_static_draw_data(nw::gfx::CommandList* cmd, const void* data, uint32_t size);

    nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const { return fallback_texture_; }
    nw::gfx::BindlessTextureIndex fallback_albedo_index() const;
    nw::gfx::BindlessTextureIndex fallback_normal_index() const;
    nw::gfx::BindlessTextureIndex fallback_surface_index() const;
    nw::gfx::BindlessTextureIndex fallback_emissive_index() const;

    nw::gfx::Handle<nw::gfx::Pipeline> pipeline(ModelPipelineKey key) const;

    nw::gfx::Handle<nw::gfx::Buffer> dummy_storage_buffer() const { return dummy_storage_buffer_; }
    nw::gfx::Handle<nw::gfx::Buffer> plt_palette_buffer() const { return plt_palette_buffer_; }

private:
    static constexpr size_t pipeline_slot_count = static_cast<size_t>(PipelineSlot::count);

    nw::gfx::Handle<nw::gfx::Pipeline> pipeline(PipelineSlot slot) const
    {
        return pipelines_[static_cast<size_t>(slot)];
    }

    nw::gfx::Handle<nw::gfx::Pipeline>& pipeline_slot(PipelineSlot slot)
    {
        return pipelines_[static_cast<size_t>(slot)];
    }

    nw::gfx::Handle<nw::gfx::Pipeline> create_pipeline_slot(PipelineSlot slot, const nw::gfx::PipelineDesc& desc);
    [[nodiscard]] bool require_pipeline_slot(PipelineSlot slot, const nw::gfx::PipelineDesc& desc, const char* message);
    bool warn_pipeline_slot(PipelineSlot slot, const nw::gfx::PipelineDesc& desc, const char* message);
    [[nodiscard]] bool initialize_common_storage_resources();
    [[nodiscard]] bool initialize_nwn_legacy_resources(nw::render::ShaderProvider& shader_provider);
    [[nodiscard]] bool initialize_render_model_pbr_resources(nw::render::ShaderProvider& shader_provider);
    void initialize_shared_fallback_textures();
    void destroy_common_frame_storage();
    void destroy_common_gpu_resources();

    nw::gfx::Context* ctx_ = nullptr;
    std::array<nw::gfx::Handle<nw::gfx::Pipeline>, pipeline_slot_count> pipelines_{};
    nw::render::PipelineCache pipeline_cache_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_texture_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_normal_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_surface_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_emissive_;
    nw::gfx::Handle<nw::gfx::Buffer> dummy_storage_buffer_;
    nw::gfx::Handle<nw::gfx::Buffer> plt_palette_buffer_;
    std::array<nw::render::FrameStorageArena, nw::gfx::kFramesInFlight> bone_arenas_{};
    std::array<nw::render::FrameStorageArena, nw::gfx::kFramesInFlight> static_draw_arenas_{};
    std::array<nw::render::FrameStorageArena, nw::gfx::kFramesInFlight> indirect_draw_arenas_{};
};

} // namespace nw::render
