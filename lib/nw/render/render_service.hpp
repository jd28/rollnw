#pragma once

#include "nwn/model_gpu_backend.hpp"
#include "nwn/render_asset_cache.hpp"
#include "renderer.hpp"

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
    void shutdown_renderer();

    Renderer& renderer();
    const Renderer& renderer() const;
    nwn::ModelGpuBackend& model_backend();
    const nwn::ModelGpuBackend& model_backend() const;
    nwn::RenderAssetCache& asset_cache();
    const nwn::RenderAssetCache& asset_cache() const;

    void initialize(kernel::ServiceInitTime time) override;
    nlohmann::json stats() const override;

private:
    bool initialize_runtime();

    nw::gfx::Context* ctx_ = nullptr;
    ShaderProvider* shader_provider_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<nwn::ModelGpuBackend> model_backend_;
    std::unique_ptr<nwn::RenderAssetCache> asset_cache_;
    bool initialized_ = false;
};

RenderService& render_service();

} // namespace nw::render
