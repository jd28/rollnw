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

enum class ModelPipelineMeshKind : uint8_t {
    pbr_static,
    pbr_skinned,
};

enum class ModelPipelinePass : uint8_t {
    color,
    shadow,
};

struct ModelPipelineKey {
    ModelPipelineMeshKind mesh = ModelPipelineMeshKind::pbr_static;
    MaterialMode material = MaterialMode::opaque;
    ModelPipelinePass pass = ModelPipelinePass::color;
    MaterialLightingModel lighting = MaterialLightingModel::pbr;
};

class ModelGpuBackend {
    enum class PipelineSlot : uint8_t {
        pbr_static_opaque,
        pbr_static_cutout,
        pbr_static_transparent,
        static_water,
        pbr_static_shadow,
        pbr_static_shadow_cutout,
        pbr_skinned_opaque,
        pbr_skinned_cutout,
        pbr_skinned_transparent,
        pbr_skinned_shadow,
        pbr_skinned_shadow_cutout,
        nwn_static_opaque,
        nwn_static_cutout,
        nwn_static_transparent,
        nwn_skinned_opaque,
        nwn_skinned_cutout,
        nwn_skinned_transparent,
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

    nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const { return fallback_texture_; }
    nw::gfx::Handle<nw::gfx::Texture> default_albedo_texture() const { return default_albedo_; }
    nw::gfx::BindlessTextureIndex fallback_albedo_index() const;
    nw::gfx::BindlessTextureIndex missing_albedo_index() const;
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
    [[nodiscard]] bool initialize_render_model_pbr_resources(nw::render::ShaderProvider& shader_provider);
    void initialize_shared_fallback_textures();
    void destroy_common_frame_storage();
    void destroy_common_gpu_resources();

    nw::gfx::Context* ctx_ = nullptr;
    std::array<nw::gfx::Handle<nw::gfx::Pipeline>, pipeline_slot_count> pipelines_{};
    nw::render::PipelineCache pipeline_cache_;
    nw::gfx::Handle<nw::gfx::Texture> default_albedo_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_texture_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_normal_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_surface_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_emissive_;
    nw::gfx::Handle<nw::gfx::Buffer> dummy_storage_buffer_;
    nw::gfx::Handle<nw::gfx::Buffer> plt_palette_buffer_;
    std::array<nw::render::FrameStorageArena, nw::gfx::kFramesInFlight> bone_arenas_{};
    std::array<nw::render::FrameStorageArena, nw::gfx::kFramesInFlight> frame_storage_arenas_{};
};

} // namespace nw::render
