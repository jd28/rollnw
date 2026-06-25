#pragma once

#include "render_context.hpp"

#include <nw/gfx/gfx.hpp>

#include <array>
#include <cstdint>

namespace nw::render {

class ShaderProvider;

// Owns the depth targets for local-light shadows: one D32F map per shadow slot
// (kLocalShadowCount). Mirrors ShadowRenderer; the per-caster perspective
// matrices live in SceneLocalShadows, produced by resolve_local_shadows.
class LocalShadowRenderer {
public:
    explicit LocalShadowRenderer(nw::gfx::Context* ctx) noexcept;
    ~LocalShadowRenderer();

    [[nodiscard]] bool initialize(ShaderProvider& shader_provider) noexcept;
    [[nodiscard]] bool ensure_resources(uint32_t resolution);

    [[nodiscard]] nw::gfx::Handle<nw::gfx::RenderTarget> render_target(uint32_t slot) const;
    [[nodiscard]] nw::gfx::Handle<nw::gfx::Texture> depth_texture(uint32_t slot) const;

private:
    nw::gfx::Context* ctx_ = nullptr;
    std::array<nw::gfx::Handle<nw::gfx::Texture>, kLocalShadowCount> depth_textures_{};
    std::array<nw::gfx::Handle<nw::gfx::RenderTarget>, kLocalShadowCount> render_targets_{};
    uint32_t map_resolution_ = 0;
};

} // namespace nw::render
