#include "render_service.hpp"

#include <nw/render/nwn/model_renderer.hpp>

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

void RenderService::clear_asset_cache()
{
    if (!asset_cache_) {
        return;
    }

    asset_cache_->clear();
}

void RenderService::shutdown_renderer()
{
    if (asset_cache_) {
        asset_cache_->clear();
    }
    asset_cache_.reset();
    nwn_model_gpu_resources_.reset();
    model_backend_.reset();
    particle_renderer_.reset();
    fog_renderer_.reset();
    local_shadow_renderer_.reset();
    shadow_renderer_.reset();
    forward_plus_renderer_.reset();
    initialized_ = false;
}

ForwardPlusRenderer& RenderService::forward_plus_renderer()
{
    return *forward_plus_renderer_;
}

const ForwardPlusRenderer& RenderService::forward_plus_renderer() const
{
    return *forward_plus_renderer_;
}

ShadowRenderer& RenderService::shadow_renderer()
{
    return *shadow_renderer_;
}

const ShadowRenderer& RenderService::shadow_renderer() const
{
    return *shadow_renderer_;
}

LocalShadowRenderer& RenderService::local_shadow_renderer()
{
    return *local_shadow_renderer_;
}

const LocalShadowRenderer& RenderService::local_shadow_renderer() const
{
    return *local_shadow_renderer_;
}

FogRenderer& RenderService::fog_renderer()
{
    return *fog_renderer_;
}

const FogRenderer& RenderService::fog_renderer() const
{
    return *fog_renderer_;
}

ParticleRenderer& RenderService::particle_renderer()
{
    return *particle_renderer_;
}

const ParticleRenderer& RenderService::particle_renderer() const
{
    return *particle_renderer_;
}

ModelGpuBackend& RenderService::model_backend()
{
    return *model_backend_;
}

const ModelGpuBackend& RenderService::model_backend() const
{
    return *model_backend_;
}

nwn::RenderAssetCache& RenderService::asset_cache()
{
    asset_cache_->sync_resource_generation();
    return *asset_cache_;
}

ModelRenderContext RenderService::model_render_context() const
{
    return {.gfx = ctx_, .gpu = model_backend_.get()};
}

nwn::ModelRenderContext RenderService::nwn_model_render_context()
{
    asset_cache_->sync_resource_generation();
    return {
        .gfx = ctx_,
        .gpu = model_backend_.get(),
        .assets = asset_cache_.get(),
        .legacy_gpu = nwn_model_gpu_resources_.get(),
    };
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
    if (asset_cache_) {
        const auto cache_stats = asset_cache_->stats();
        j["asset_cache"]["epoch"] = cache_stats.epoch;
        j["asset_cache"]["invalidation_count"] = cache_stats.invalidation_count;
        j["asset_cache"]["observed_resource_generation"] = cache_stats.observed_resource_generation;
        j["asset_cache"]["current_resource_generation"] = cache_stats.current_resource_generation;
        j["asset_cache"]["texture_count"] = cache_stats.texture_count;
        j["asset_cache"]["owned_texture_count"] = cache_stats.owned_texture_count;
        j["asset_cache"]["texture_upload_bytes"] = cache_stats.texture_upload_bytes;
        j["asset_cache"]["source_image_count"] = cache_stats.source_image_count;
        j["asset_cache"]["source_image_pixel_bytes"] = cache_stats.source_image_pixel_bytes;
        j["asset_cache"]["particle_mesh_count"] = cache_stats.particle_mesh_count;
        j["asset_cache"]["particle_mesh_payload_bytes"] = cache_stats.particle_mesh_payload_bytes;
    }
    const auto loader_cache_stats = nwn::model_loader_resource_cache_stats();
    j["nwn_model_loader_resource_cache"]["mtr_material_count"] = loader_cache_stats.mtr_material_count;
    j["nwn_model_loader_resource_cache"]["texture_analysis_count"] = loader_cache_stats.texture_analysis_count;
    return j;
}

bool RenderService::initialize_runtime()
{
    if (!ctx_ || !shader_provider_) {
        LOG_F(ERROR, "render service: missing context or shader provider");
        return false;
    }

    forward_plus_renderer_ = std::make_unique<ForwardPlusRenderer>(ctx_);
    if (!forward_plus_renderer_->initialize(*shader_provider_)) {
        forward_plus_renderer_.reset();
        return false;
    }

    shadow_renderer_ = std::make_unique<ShadowRenderer>(ctx_);
    if (!shadow_renderer_->initialize(*shader_provider_)) {
        forward_plus_renderer_.reset();
        shadow_renderer_.reset();
        return false;
    }

    local_shadow_renderer_ = std::make_unique<LocalShadowRenderer>(ctx_);
    if (!local_shadow_renderer_->initialize(*shader_provider_)) {
        forward_plus_renderer_.reset();
        shadow_renderer_.reset();
        local_shadow_renderer_.reset();
        return false;
    }

    fog_renderer_ = std::make_unique<FogRenderer>(ctx_);
    if (!fog_renderer_->initialize(*shader_provider_)) {
        forward_plus_renderer_.reset();
        shadow_renderer_.reset();
        local_shadow_renderer_.reset();
        fog_renderer_.reset();
        return false;
    }

    particle_renderer_ = std::make_unique<ParticleRenderer>(ctx_);
    if (!particle_renderer_->initialize(*shader_provider_)) {
        forward_plus_renderer_.reset();
        shadow_renderer_.reset();
        local_shadow_renderer_.reset();
        fog_renderer_.reset();
        particle_renderer_.reset();
        return false;
    }

    model_backend_ = std::make_unique<ModelGpuBackend>(ctx_);
    if (!model_backend_->initialize(*shader_provider_)) {
        forward_plus_renderer_.reset();
        shadow_renderer_.reset();
        local_shadow_renderer_.reset();
        fog_renderer_.reset();
        particle_renderer_.reset();
        model_backend_.reset();
        return false;
    }

    nwn_model_gpu_resources_ = std::make_unique<nwn::ModelGpuResources>(ctx_);
    if (!nwn_model_gpu_resources_->initialize()) {
        forward_plus_renderer_.reset();
        shadow_renderer_.reset();
        local_shadow_renderer_.reset();
        fog_renderer_.reset();
        particle_renderer_.reset();
        nwn_model_gpu_resources_.reset();
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
