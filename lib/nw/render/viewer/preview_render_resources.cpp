#include "preview_render_resources.hpp"

#include <nw/log.hpp>

namespace nw::render::viewer {

PreviewRenderResources::PreviewRenderResources(nw::gfx::Context* ctx)
    : ctx_(ctx)
{
}

PreviewRenderResources::~PreviewRenderResources()
{
}

bool PreviewRenderResources::initialize(nw::render::ShaderProvider* shader_provider)
{
    static_cast<void>(shader_provider);
    auto* service = nw::kernel::services().get_mut<nw::render::RenderService>();
    if (!service) {
        LOG_F(ERROR, "Render service is not registered");
        return false;
    }
    if (!service->ensure_initialized()) {
        LOG_F(ERROR, "Render service is not initialized");
        return false;
    }
    render_service_ = service;

    LOG_F(INFO, "PreviewRenderResources initialized");
    return true;
}

ModelGpuBackend& PreviewRenderResources::model_backend() const
{
    return render_service_->model_backend();
}

RenderAssetCache& PreviewRenderResources::asset_cache() const
{
    return render_service_->asset_cache();
}

nw::gfx::Handle<nw::gfx::Texture> PreviewRenderResources::get_or_load_texture(const std::string& name, bool premultiply_alpha)
{
    return asset_cache().get_or_load_texture(name, premultiply_alpha, model_backend().fallback_texture());
}

nw::gfx::Handle<nw::gfx::Texture> PreviewRenderResources::get_or_load_texture(const std::string& name, const nw::PltColors& colors, bool premultiply_alpha)
{
    return asset_cache().get_or_load_texture(name, colors, premultiply_alpha, model_backend().fallback_texture());
}

nw::gfx::Handle<nw::gfx::Texture> PreviewRenderResources::get_or_load_raw_plt_texture(const std::string& name)
{
    return asset_cache().get_or_load_raw_plt_texture(name);
}

} // namespace nw::render::viewer
