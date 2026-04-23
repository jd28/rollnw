#include "render_service.hpp"

#include <nw/log.hpp>

#include <nlohmann/json.hpp>

namespace nw::render {

const std::type_index RenderService::type_index{typeid(RenderService)};

RenderService::RenderService(MemoryResource* memory)
    : Service(memory)
{
}

void RenderService::configure(nw::gfx::Context* ctx, ShaderProvider* shader_provider)
{
    if (ctx_ == ctx && shader_provider_ == shader_provider) {
        return;
    }

    if (initialized_) {
        shutdown_renderer();
    }

    ctx_ = ctx;
    shader_provider_ = shader_provider;
}

bool RenderService::ensure_initialized()
{
    if (initialized_) {
        return true;
    }
    return initialize_runtime();
}

bool RenderService::reload_shaders()
{
    if (!ctx_ || !shader_provider_) {
        LOG_F(ERROR, "render service: missing context or shader provider");
        return false;
    }

    nw::gfx::wait_idle(ctx_);
    shutdown_renderer();
    shader_provider_->clear_cache();

    if (!ensure_initialized()) {
        LOG_F(ERROR, "render service: shader reload failed");
        return false;
    }

    LOG_F(INFO, "render service: shaders reloaded");
    return true;
}

void RenderService::shutdown_renderer()
{
    if (asset_cache_ && model_backend_) {
        asset_cache_->destroy(model_backend_->fallback_texture());
    }
    asset_cache_.reset();
    model_backend_.reset();
    renderer_.reset();
    initialized_ = false;
}

Renderer& RenderService::renderer()
{
    return *renderer_;
}

const Renderer& RenderService::renderer() const
{
    return *renderer_;
}

nwn::ModelGpuBackend& RenderService::model_backend()
{
    return *model_backend_;
}

const nwn::ModelGpuBackend& RenderService::model_backend() const
{
    return *model_backend_;
}

nwn::RenderAssetCache& RenderService::asset_cache()
{
    return *asset_cache_;
}

const nwn::RenderAssetCache& RenderService::asset_cache() const
{
    return *asset_cache_;
}

void RenderService::initialize(kernel::ServiceInitTime time)
{
    if (time != kernel::ServiceInitTime::kernel_start) {
        return;
    }
    if (!ctx_ || !shader_provider_) {
        LOG_F(INFO, "render service: waiting for context/shader provider configuration");
        return;
    }
    if (!ensure_initialized()) {
        LOG_F(ERROR, "render service: initialization failed");
    }
}

nlohmann::json RenderService::stats() const
{
    nlohmann::json j;
    j["name"] = "render service";
    j["initialized"] = initialized_;
    j["configured"] = ctx_ != nullptr && shader_provider_ != nullptr;
    return j;
}

bool RenderService::initialize_runtime()
{
    if (!ctx_ || !shader_provider_) {
        LOG_F(ERROR, "render service: missing context or shader provider");
        return false;
    }

    renderer_ = std::make_unique<Renderer>(ctx_);
    if (!renderer_->initialize(*shader_provider_)) {
        renderer_.reset();
        return false;
    }

    model_backend_ = std::make_unique<nwn::ModelGpuBackend>(ctx_);
    if (!model_backend_->initialize(*shader_provider_)) {
        renderer_.reset();
        model_backend_.reset();
        return false;
    }

    asset_cache_ = std::make_unique<nwn::RenderAssetCache>(ctx_);
    initialized_ = true;
    return true;
}

RenderService& render_service()
{
    auto* res = nw::kernel::services().get_mut<RenderService>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load render service");
    }
    return *res;
}

} // namespace nw::render
