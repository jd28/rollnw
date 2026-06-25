#pragma once

#include "frame_storage_arena.hpp"
#include "render_context.hpp"

#include <nw/formats/Image.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/render/particle_def.hpp>
#include <nw/render/particle_system.hpp>

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace nw::render {

class ShaderProvider;

struct ParticleBillboardAxes {
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 0.0f, 1.0f};
};

struct ParticleSpriteFrameRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    uint16_t column = 0;
    uint16_t row = 0;
};

[[nodiscard]] ParticleSpriteFrameRect particle_sprite_sheet_frame_rect(
    const ParticleSpriteSheet& sheet, uint16_t frame, const nw::Image* source_image = nullptr,
    bool flip_rows = false) noexcept;

[[nodiscard]] ParticleBillboardAxes resolve_particle_billboard_axes(const RenderContext& ctx,
    const ParticleSystemInstance& system, const ParticleRenderPacket& packet, uint32_t particle_index,
    const ParticleBillboardAxes& fallback) noexcept;

[[nodiscard]] ParticleBillboardAxes particle_billboard_sprite_axes(
    ParticleRenderMode mode, const ParticleBillboardAxes& axes, float rotation) noexcept;

class ParticleResourceProvider {
public:
    virtual ~ParticleResourceProvider() = default;

    virtual nw::gfx::Handle<nw::gfx::Texture> get_texture(std::string_view name, bool premultiply_alpha) = 0;
    virtual const nw::Image* get_source_image(std::string_view name) = 0;
    virtual bool texture_rows_flipped(std::string_view name, bool premultiply_alpha) = 0;
    virtual nw::gfx::Handle<nw::gfx::Texture> fallback_texture() const = 0;
};

class ParticleRenderer {
public:
    explicit ParticleRenderer(nw::gfx::Context* ctx) noexcept;
    ~ParticleRenderer();

    [[nodiscard]] bool initialize(ShaderProvider& shader_provider);
    void render(nw::gfx::CommandList* cmd, const ParticleSystemInstance& system,
        std::span<const ParticleMaterialDef> source_materials, const RenderContext& ctx,
        ParticleResourceProvider& resources);

private:
    [[nodiscard]] nw::gfx::StorageSpan upload_instances(
        nw::gfx::CommandList* cmd, std::span<const ParticleBillboardInstance> instances);

    nw::gfx::Context* ctx_ = nullptr;
    nw::gfx::Handle<nw::gfx::Pipeline> path_additive_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> path_cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> path_transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> additive_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> cutout_pipeline_;
    nw::gfx::Handle<nw::gfx::Pipeline> transparent_pipeline_;
    nw::gfx::Handle<nw::gfx::Buffer> billboard_vertices_;
    std::array<FrameStorageArena, 2> instance_arenas_{};
    std::vector<ParticleVertex> vertices_;
    std::vector<ParticleBillboardInstance> instances_;
    std::vector<uint32_t> order_;
    std::vector<uint32_t> chain_order_;
};

} // namespace nw::render
