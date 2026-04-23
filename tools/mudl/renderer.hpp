#pragma once

#include <nw/render/nwn/model_gpu_backend.hpp>
#include <nw/render/nwn/model_loader.hpp>
#include <nw/render/nwn/render_asset_cache.hpp>

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/render/render_context.hpp>
#include <nw/render/render_service.hpp>
#include <nw/render/renderer.hpp>
#include <nw/render/model.hpp>
#include <nw/render/particle_system.hpp>
#include <nw/render/shader_provider.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <string>
#include <vector>

namespace mudl {

static constexpr int kMaxBones = 64;

struct PreviewScene;
using nw::render::nwn::ModelGpuBackend;
using nw::render::nwn::ModelInstance;
using nw::render::nwn::RenderAssetCache;
using nw::render::nwn::Vertex;

using nw::render::FogCompositeConstants;
using nw::render::kShadowCascadeCount;
using nw::render::Lighting;
using nw::render::LightingSpace;
using nw::render::RenderPassSelection;
using nw::render::RenderContext;
using nw::render::SceneConstants;
using nw::render::SceneFog;
using nw::render::SceneShadow;
using nw::render::ShadowConstants;

// Model renderer
class Renderer : public nw::render::Renderer::ParticleResourceProvider {
public:
    explicit Renderer(nw::gfx::Context* ctx);
    ~Renderer();

    bool initialize(nw::render::ShaderProvider* shader_provider);

    void render_model(nw::gfx::CommandList* cmd, const ModelInstance& model, const RenderContext& ctx,
        RenderPassSelection pass = RenderPassSelection::all);
    void render_static_model(nw::gfx::CommandList* cmd, const nw::render::RenderModel& model, const RenderContext& ctx,
        RenderPassSelection pass = RenderPassSelection::all,
        const std::vector<std::vector<glm::mat4>>* skin_matrices = nullptr);
    void render_shadow_model(nw::gfx::CommandList* cmd, const ModelInstance& model, const glm::mat4& light_view,
        const glm::mat4& light_projection);
    void render_debug_grid(nw::gfx::CommandList* cmd, const PreviewScene& scene, const RenderContext& ctx,
        float spacing, float major_interval, float minor_width, float major_width, float opacity, float z_offset);
    void render_particles(nw::gfx::CommandList* cmd, const PreviewScene& scene, const RenderContext& ctx);
    bool ensure_shadow_resources(uint32_t resolution) { return model_backend().ensure_shadow_resources(resolution); }
    nw::gfx::Handle<nw::gfx::RenderTarget> shadow_render_target(uint32_t cascade) const { return model_backend().shadow_render_target(cascade); }
    nw::gfx::Handle<nw::gfx::Texture> shadow_depth_texture(uint32_t cascade) const { return model_backend().shadow_depth_texture(cascade); }
    bool shadow_pipeline_ready() const { return model_backend().static_shadow_pipeline().valid(); }
    bool ensure_fog_resources(uint32_t width, uint32_t height) { return render_backend().ensure_fog_resources(width, height); }
    void render_fog_composite(nw::gfx::CommandList* cmd, const RenderContext& ctx, const SceneFog& fog) { render_backend().render_fog_composite(cmd, ctx, fog); }
    nw::gfx::Handle<nw::gfx::RenderTarget> fog_render_target() const { return render_backend().fog_render_target(); }
    bool fog_pipeline_ready() const { return render_backend().fog_pipeline_ready(); }

    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(const std::string& name, bool premultiply_alpha = false);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(const std::string& name, const nw::PltColors& colors, bool premultiply_alpha = false);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_raw_plt_texture(const std::string& name);
    const nw::Image* get_or_load_source_image(const std::string& name);
    nw::gfx::Handle<nw::gfx::Texture> get_texture(std::string_view name, bool premultiply_alpha) override;
    const nw::Image* get_source_image(std::string_view name) override;
    nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const override { return model_backend().fallback_texture(); }
    nw::gfx::StorageSpan upload_bones(nw::gfx::CommandList* cmd, const glm::mat4* bones, uint32_t count) { return model_backend().upload_bones(cmd, bones, count); }
    nw::gfx::Context* context() const { return ctx_; }
    nw::gfx::BindlessTextureIndex fallback_albedo_index() const { return model_backend().fallback_albedo_index(); }
    nw::gfx::BindlessTextureIndex fallback_normal_index() const { return model_backend().fallback_normal_index(); }
    nw::gfx::BindlessTextureIndex fallback_surface_index() const { return model_backend().fallback_surface_index(); }
    nw::gfx::BindlessTextureIndex fallback_emissive_index() const { return model_backend().fallback_emissive_index(); }
    nw::gfx::Handle<nw::gfx::Pipeline> static_pipeline() const { return model_backend().static_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> static_shadow_pipeline() const { return model_backend().static_shadow_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> static_shadow_cutout_pipeline() const { return model_backend().static_shadow_cutout_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> static_offscreen_pipeline() const { return model_backend().static_offscreen_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> static_offscreen_transparent_pipeline() const { return model_backend().static_offscreen_transparent_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> static_transparent_pipeline() const { return model_backend().static_transparent_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_pipeline() const { return model_backend().skinned_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_shadow_pipeline() const { return model_backend().skinned_shadow_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_shadow_cutout_pipeline() const { return model_backend().skinned_shadow_cutout_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_offscreen_pipeline() const { return model_backend().skinned_offscreen_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_offscreen_transparent_pipeline() const { return model_backend().skinned_offscreen_transparent_pipeline(); }
    nw::gfx::Handle<nw::gfx::Pipeline> skinned_transparent_pipeline() const { return model_backend().skinned_transparent_pipeline(); }
    nw::gfx::Handle<nw::gfx::Buffer> plt_palette_buffer() const { return model_backend().plt_palette_buffer(); }

private:
    ModelInstance* get_or_load_particle_mesh(const std::string& resref);
    nw::render::Renderer& render_backend() const;
    ModelGpuBackend& model_backend() const;
    RenderAssetCache& asset_cache() const;

    nw::gfx::Context* ctx_ = nullptr;
    nw::render::RenderService* render_service_ = nullptr;
    nw::render::ShaderProvider* shader_provider_ = nullptr;
    nw::gfx::Handle<nw::gfx::Pipeline> debug_grid_pipeline_;
    nw::gfx::Handle<nw::gfx::Buffer> debug_grid_vertices_;
    nw::gfx::Handle<nw::gfx::Buffer> debug_grid_indices_;
    size_t debug_grid_vertex_capacity_ = 0;
    size_t debug_grid_index_capacity_ = 0;
};

} // namespace mudl
