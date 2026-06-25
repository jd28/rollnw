#pragma once

#include "render_context.hpp"

#include <nw/gfx/gfx.hpp>

#include <array>
#include <cstdint>

namespace nw::render {

class ShaderProvider;

class ShadowRenderer {
public:
    explicit ShadowRenderer(nw::gfx::Context* ctx) noexcept;
    ~ShadowRenderer();

    [[nodiscard]] bool initialize(ShaderProvider& shader_provider) noexcept;
    [[nodiscard]] bool ensure_resources(uint32_t resolution);

    [[nodiscard]] nw::gfx::Handle<nw::gfx::RenderTarget> render_target(uint32_t cascade) const;
    [[nodiscard]] nw::gfx::Handle<nw::gfx::Texture> depth_texture(uint32_t cascade) const;

private:
    nw::gfx::Context* ctx_ = nullptr;
    std::array<nw::gfx::Handle<nw::gfx::Texture>, kShadowCascadeCount> depth_textures_{};
    std::array<nw::gfx::Handle<nw::gfx::RenderTarget>, kShadowCascadeCount> render_targets_{};
    uint32_t map_resolution_ = 0;
};

} // namespace nw::render
