#pragma once

#include "render_context.hpp"

#include <nw/gfx/gfx.hpp>

#include <cstdint>

namespace nw::render {

class ShaderProvider;

class FogRenderer {
public:
    explicit FogRenderer(nw::gfx::Context* ctx) noexcept;
    ~FogRenderer();

    [[nodiscard]] bool initialize(ShaderProvider& shader_provider);
    [[nodiscard]] bool ensure_resources(uint32_t width, uint32_t height);
    void render_composite(nw::gfx::CommandList* cmd, const RenderContext& ctx, const SceneFog& fog);

    [[nodiscard]] nw::gfx::Handle<nw::gfx::RenderTarget> render_target() const noexcept { return render_target_; }
    [[nodiscard]] bool pipeline_ready() const noexcept { return pipeline_.valid() && vertices_.valid(); }

private:
    nw::gfx::Context* ctx_ = nullptr;
    nw::gfx::Handle<nw::gfx::Pipeline> pipeline_;
    nw::gfx::Handle<nw::gfx::Buffer> vertices_;
    nw::gfx::Handle<nw::gfx::Texture> color_texture_;
    nw::gfx::Handle<nw::gfx::Texture> depth_texture_;
    nw::gfx::Handle<nw::gfx::RenderTarget> render_target_;
    uint32_t target_width_ = 0;
    uint32_t target_height_ = 0;
};

} // namespace nw::render
