#pragma once

#include <nw/render/model_gpu_backend.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/nwn/render_asset_cache.hpp>

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/render/model.hpp>
#include <nw/render/nwn/model_renderer.hpp>
#include <nw/render/render_context.hpp>
#include <nw/render/render_service.hpp>
#include <nw/render/shader_provider.hpp>

#include <string>

namespace nw::render::viewer {

class AreaRenderFrame;
class AreaRenderScene;
struct PreviewScene;
struct SceneParticleSystem;
using nw::render::ModelGpuBackend;
using nw::render::nwn::ModelInstance;
using nw::render::nwn::RenderAssetCache;
using nw::render::nwn::Vertex;

using nw::render::Lighting;
using nw::render::LightingSpace;
using nw::render::RenderContext;
using nw::render::RenderPassSelection;
using nw::render::SceneEnvironment;
using nw::render::SceneFog;

// Resource facade used while preview scenes are being loaded.
class PreviewRenderResources {
public:
    explicit PreviewRenderResources(nw::gfx::Context* ctx);
    ~PreviewRenderResources();

    bool initialize(nw::render::ShaderProvider* shader_provider);
    [[nodiscard]] nw::gfx::Context* gfx_context() const noexcept { return ctx_; }

    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(const std::string& name, bool premultiply_alpha = false);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(const std::string& name, const nw::PltColors& colors, bool premultiply_alpha = false);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_raw_plt_texture(const std::string& name);
    nw::gfx::Context* context() const { return ctx_; }
    nw::gfx::BindlessTextureIndex fallback_albedo_index() const { return model_backend().fallback_albedo_index(); }
    nw::gfx::BindlessTextureIndex fallback_normal_index() const { return model_backend().fallback_normal_index(); }
    nw::gfx::BindlessTextureIndex fallback_surface_index() const { return model_backend().fallback_surface_index(); }
    nw::gfx::BindlessTextureIndex fallback_emissive_index() const { return model_backend().fallback_emissive_index(); }

private:
    ModelGpuBackend& model_backend() const;
    RenderAssetCache& asset_cache() const;

    nw::gfx::Context* ctx_ = nullptr;
    nw::render::RenderService* render_service_ = nullptr;
};

} // namespace nw::render::viewer
