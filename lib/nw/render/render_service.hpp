#pragma once

#include "fog_renderer.hpp"
#include "forward_plus_renderer.hpp"
#include "local_shadow_renderer.hpp"
#include "model_gpu_backend.hpp"
#include "model_render_context.hpp"
#include "nwn/model_gpu_backend.hpp"
#include "nwn/model_render_context.hpp"
#include "nwn/render_asset_cache.hpp"
#include "particle_renderer.hpp"
#include "shadow_renderer.hpp"

#include <nw/kernel/Kernel.hpp>

#include <memory>
#include <typeindex>

namespace nw::render {

struct RenderService : public kernel::Service {
    const static std::type_index type_index;

    explicit RenderService(MemoryResource* memory);

    void configure(nw::gfx::Context* ctx, ShaderProvider* shader_provider);
    bool ensure_initialized();
    bool reload_shaders();
    void clear_asset_cache();
    void shutdown_renderer();

    ForwardPlusRenderer& forward_plus_renderer();
    const ForwardPlusRenderer& forward_plus_renderer() const;
    ShadowRenderer& shadow_renderer();
    const ShadowRenderer& shadow_renderer() const;
    LocalShadowRenderer& local_shadow_renderer();
    const LocalShadowRenderer& local_shadow_renderer() const;
    FogRenderer& fog_renderer();
    const FogRenderer& fog_renderer() const;
    ParticleRenderer& particle_renderer();
    const ParticleRenderer& particle_renderer() const;
    ModelGpuBackend& model_backend();
    const ModelGpuBackend& model_backend() const;
    [[nodiscard]] ModelRenderContext model_render_context() const;
    [[nodiscard]] nwn::ModelRenderContext nwn_model_render_context() const;
    nwn::RenderAssetCache& asset_cache();
    const nwn::RenderAssetCache& asset_cache() const;

    void initialize(kernel::ServiceInitTime time) override;
    nlohmann::json stats() const override;

private:
    bool initialize_runtime();

    nw::gfx::Context* ctx_ = nullptr;
    ShaderProvider* shader_provider_ = nullptr;
    std::unique_ptr<ForwardPlusRenderer> forward_plus_renderer_;
    std::unique_ptr<ShadowRenderer> shadow_renderer_;
    std::unique_ptr<LocalShadowRenderer> local_shadow_renderer_;
    std::unique_ptr<FogRenderer> fog_renderer_;
    std::unique_ptr<ParticleRenderer> particle_renderer_;
    std::unique_ptr<ModelGpuBackend> model_backend_;
    std::unique_ptr<nwn::ModelGpuResources> nwn_model_gpu_resources_;
    std::unique_ptr<nwn::RenderAssetCache> asset_cache_;
    bool initialized_ = false;
};

RenderService& render_service();

} // namespace nw::render
