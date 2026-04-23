#pragma once

#include "render_context.hpp"
#include "shader_provider.hpp"

#include <nw/formats/Image.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/render/model.hpp>
#include <nw/render/particle_def.hpp>
#include <nw/render/particle_system.hpp>

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace nw::render {

enum class RenderPassSelection {
    opaque_cutout,
    transparent,
    all,
};

class Renderer {
public:
    class ParticleResourceProvider {
    public:
        virtual ~ParticleResourceProvider() = default;

        virtual nw::gfx::Handle<nw::gfx::Texture> get_texture(std::string_view name, bool premultiply_alpha) = 0;
        virtual const nw::Image* get_source_image(std::string_view name) = 0;
        virtual nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const = 0;
    };

    explicit Renderer(nw::gfx::Context* ctx) noexcept;
    ~Renderer();

    [[nodiscard]] nw::gfx::Context* context() const noexcept;
    [[nodiscard]] bool initialize(ShaderProvider& shader_provider);
    void render_static_model(nw::gfx::CommandList* cmd, const RenderModel& model, const RenderContext& ctx,
        RenderPassSelection pass = RenderPassSelection::all,
        const std::vector<std::vector<glm::mat4>>* skin_matrices = nullptr);
    void render_particles(nw::gfx::CommandList* cmd, const ParticleSystemInstance& system,
        std::span<const ParticleMaterialDef> source_materials, const RenderContext& ctx,
        ParticleResourceProvider& resources);
    [[nodiscard]] bool ensure_fog_resources(uint32_t width, uint32_t height);
    void render_fog_composite(nw::gfx::CommandList* cmd, const RenderContext& ctx, const SceneFog& fog);
    [[nodiscard]] nw::gfx::Handle<nw::gfx::RenderTarget> fog_render_target() const noexcept { return fog_render_target_; }
    [[nodiscard]] bool fog_pipeline_ready() const noexcept { return fog_composite_pipeline_.valid() && fog_composite_vertices_.valid(); }

private:
    struct StorageBlock {
        nw::gfx::Handle<nw::gfx::Buffer> buffer;
        void* mapped = nullptr;
        size_t capacity = 0;
        size_t offset = 0;
    };

    struct StorageFrameArena {
        std::vector<StorageBlock> blocks;
        uint64_t frame_id = UINT64_MAX;

        nw::gfx::StorageSpan write(nw::gfx::Context* ctx, const void* data, uint32_t size, uint32_t alignment = 64);
        bool reset(nw::gfx::Context* ctx, uint64_t new_frame_id, size_t min_capacity = 0);
        void destroy();

    private:
        nw::gfx::StorageSpan allocate(nw::gfx::Context* ctx, uint32_t size, uint32_t alignment);
    };

    nw::gfx::StorageSpan upload_bones(nw::gfx::CommandList* cmd, const glm::mat4* bones, uint32_t count);
    nw::gfx::StorageSpan upload_particle_instances(nw::gfx::CommandList* cmd,
        std::span<const ParticleBillboardInstance> instances);
    nw::gfx::Context* ctx_ = nullptr;
    ShaderProvider* shader_provider_ = nullptr;
    nw::gfx::Handle<nw::gfx::Pipeline> pbr_opaque_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> pbr_cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> pbr_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> pbr_skinned_opaque_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> pbr_skinned_cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> pbr_skinned_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> particle_path_additive_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> particle_path_cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> particle_path_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> particle_additive_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> particle_cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> particle_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> fog_composite_pipeline_;
    nw::gfx::Handle<nw::gfx::Buffer> fog_composite_vertices_;
    nw::gfx::Handle<nw::gfx::Buffer> particle_billboard_vertices_;
    nw::gfx::Handle<nw::gfx::Texture> fog_color_texture_;
    nw::gfx::Handle<nw::gfx::Texture> fog_depth_texture_;
    nw::gfx::Handle<nw::gfx::RenderTarget> fog_render_target_;
    uint32_t fog_target_width_ = 0;
    uint32_t fog_target_height_ = 0;
    std::array<StorageFrameArena, 2> bone_arenas_{};
    std::array<StorageFrameArena, 2> particle_instance_arenas_{};
    std::vector<ParticleVertex> particle_vertices_;
    std::vector<ParticleBillboardInstance> particle_instances_;
    std::vector<uint32_t> particle_order_;
    std::vector<uint32_t> particle_chain_order_;
};

} // namespace nw::render
