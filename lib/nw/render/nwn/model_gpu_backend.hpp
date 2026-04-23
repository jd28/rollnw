#pragma once

#include "model_loader.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/render/render_context.hpp>
#include <nw/render/shader_provider.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace nw::render::nwn {

class ModelGpuBackend {
public:
    explicit ModelGpuBackend(nw::gfx::Context* ctx)
        : ctx_{ctx}
    {
    }

    ~ModelGpuBackend();

    bool initialize(nw::render::ShaderProvider& shader_provider);
    bool ensure_shadow_resources(uint32_t resolution);
    nw::gfx::StorageSpan upload_bones(nw::gfx::CommandList* cmd, const glm::mat4* bones, uint32_t count);

    nw::gfx::Handle<nw::gfx::RenderTarget> shadow_render_target(uint32_t cascade) const { return shadow_render_targets_[cascade]; }
    nw::gfx::Handle<nw::gfx::Texture> shadow_depth_texture(uint32_t cascade) const { return shadow_depth_textures_[cascade]; }

    nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const { return fallback_texture_; }
    nw::gfx::BindlessTextureIndex fallback_albedo_index() const;
    nw::gfx::BindlessTextureIndex fallback_normal_index() const;
    nw::gfx::BindlessTextureIndex fallback_surface_index() const;
    nw::gfx::BindlessTextureIndex fallback_emissive_index() const;

    nw::gfx::Handle<nw::gfx::Pipeline> static_pipeline() const { return static_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> static_shadow_pipeline() const { return static_shadow_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> static_shadow_cutout_pipeline() const { return static_shadow_cutout_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> static_offscreen_pipeline() const { return static_offscreen_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> static_offscreen_transparent_pipeline() const { return static_offscreen_transparent_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> static_transparent_pipeline() const { return static_transparent_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_pipeline() const { return skinned_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_shadow_pipeline() const { return skinned_shadow_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_shadow_cutout_pipeline() const { return skinned_shadow_cutout_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_offscreen_pipeline() const { return skinned_offscreen_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_offscreen_transparent_pipeline() const { return skinned_offscreen_transparent_pipeline_; }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_transparent_pipeline() const { return skinned_transparent_pipeline_; }
    nw::gfx::Handle<nw::gfx::Buffer> plt_palette_buffer() const { return plt_palette_buffer_; }

private:
    struct BoneBlock {
        nw::gfx::Handle<nw::gfx::Buffer> buffer;
        void* mapped = nullptr;
        size_t capacity = 0;
        size_t offset = 0;
    };

    struct BoneFrameArena {
        std::vector<BoneBlock> blocks;
        uint64_t frame_id = UINT64_MAX;

        nw::gfx::StorageSpan write(nw::gfx::Context* ctx, const void* data, uint32_t size, uint32_t alignment = 64);
        bool reset(nw::gfx::Context* ctx, uint64_t new_frame_id, size_t min_capacity = 0);
        void destroy();

    private:
        nw::gfx::StorageSpan allocate(nw::gfx::Context* ctx, uint32_t size, uint32_t alignment);
    };

    nw::gfx::Context* ctx_ = nullptr;
    nw::gfx::Handle<nw::gfx::Pipeline> static_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> static_shadow_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> static_shadow_cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> static_offscreen_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> static_offscreen_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> static_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_shadow_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_shadow_cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_offscreen_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_offscreen_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_texture_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_normal_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_surface_;
    nw::gfx::Handle<nw::gfx::Texture> fallback_emissive_;
    nw::gfx::Handle<nw::gfx::Buffer> plt_palette_buffer_;
    std::array<nw::gfx::Handle<nw::gfx::Texture>, nw::render::kShadowCascadeCount> shadow_depth_textures_{};
    std::array<nw::gfx::Handle<nw::gfx::RenderTarget>, nw::render::kShadowCascadeCount> shadow_render_targets_{};
    uint32_t shadow_map_resolution_ = 0;
    std::array<BoneFrameArena, 2> bone_arenas_{};
};

} // namespace nw::render::nwn
