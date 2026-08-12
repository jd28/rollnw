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
    return asset_cache().get_or_load_texture(nw::Resref{name}, premultiply_alpha, model_backend().fallback_texture());
}

nw::gfx::Handle<nw::gfx::Texture> PreviewRenderResources::get_or_load_texture(const std::string& name, const nw::PltColors& colors, bool premultiply_alpha)
{
    return asset_cache().get_or_load_texture(nw::Resref{name}, colors, premultiply_alpha, model_backend().fallback_texture());
}

nw::gfx::Handle<nw::gfx::Texture> PreviewRenderResources::get_or_load_raw_plt_texture(const std::string& name)
{
    return asset_cache().get_or_load_raw_plt_texture(nw::Resref{name});
}

void PreviewRenderResources::prepare_area_static_models(uint64_t resource_generation)
{
    if (area_static_model_resource_generation_ != resource_generation) {
        area_static_models_.clear();
        area_static_model_resource_generation_ = resource_generation;
        return;
    }

    for (auto it = area_static_models_.begin(); it != area_static_models_.end();) {
        if (it->second.expired()) {
            area_static_models_.erase(it++);
        } else {
            ++it;
        }
    }
}

std::shared_ptr<nw::render::RenderModel> PreviewRenderResources::find_area_static_model(nw::Resref name)
{
    const auto cached = area_static_models_.find(name);
    if (cached == area_static_models_.end()) {
        return {};
    }
    auto model = cached->second.lock();
    if (!model) {
        area_static_models_.erase(cached);
    }
    return model;
}

void PreviewRenderResources::store_area_static_model(
    nw::Resref name, const std::shared_ptr<nw::render::RenderModel>& model)
{
    if (!model) {
        return;
    }
    area_static_models_.insert_or_assign(name, model);
}

} // namespace nw::render::viewer
