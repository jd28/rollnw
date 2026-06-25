#include "local_shadow_renderer.hpp"

#include <nw/log.hpp>

#include <algorithm>
#include <cstddef>

namespace nw::render {

LocalShadowRenderer::LocalShadowRenderer(nw::gfx::Context* ctx) noexcept
    : ctx_(ctx)
{
}

LocalShadowRenderer::~LocalShadowRenderer()
{
    for (auto& render_target : render_targets_) {
        if (render_target.valid()) {
            nw::gfx::destroy_render_target(ctx_, render_target);
        }
    }
}

bool LocalShadowRenderer::initialize(ShaderProvider&) noexcept
{
    return ctx_ != nullptr;
}

bool LocalShadowRenderer::ensure_resources(uint32_t resolution)
{
    resolution = std::max(resolution, 1u);
    if (render_targets_[0].valid() && map_resolution_ == resolution) {
        return true;
    }

    for (size_t i = 0; i < render_targets_.size(); ++i) {
        if (render_targets_[i].valid()) {
            nw::gfx::destroy_render_target(ctx_, render_targets_[i]);
            render_targets_[i] = {};
            depth_textures_[i] = {};
        }
    }

    for (size_t i = 0; i < render_targets_.size(); ++i) {
        nw::gfx::TextureDesc depth_desc{};
        depth_desc.width = resolution;
        depth_desc.height = resolution;
        depth_desc.format = nw::gfx::Fmt::D32F;
        depth_desc.sampled = true;
        depth_desc.render_target = true;
        depth_textures_[i] = nw::gfx::create_texture(ctx_, depth_desc);
        if (!depth_textures_[i].valid()) {
            LOG_F(ERROR, "Failed to create local shadow depth target {}", i);
            return false;
        }

        nw::gfx::RenderTargetDesc rt_desc{};
        rt_desc.depth.texture = depth_textures_[i];
        render_targets_[i] = nw::gfx::create_render_target(ctx_, rt_desc);
        if (!render_targets_[i].valid()) {
            LOG_F(ERROR, "Failed to create local shadow render target {}", i);
            nw::gfx::destroy_texture(ctx_, depth_textures_[i]);
            depth_textures_[i] = {};
            return false;
        }
    }

    map_resolution_ = resolution;
    return true;
}

nw::gfx::Handle<nw::gfx::RenderTarget> LocalShadowRenderer::render_target(uint32_t slot) const
{
    if (slot >= render_targets_.size()) {
        return {};
    }
    return render_targets_[slot];
}

nw::gfx::Handle<nw::gfx::Texture> LocalShadowRenderer::depth_texture(uint32_t slot) const
{
    if (slot >= depth_textures_.size()) {
        return {};
    }
    return depth_textures_[slot];
}

} // namespace nw::render
