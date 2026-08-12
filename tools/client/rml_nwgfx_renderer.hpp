#pragma once

#include "../ui/rml_generated_texture.hpp"

#include <RmlUi/Core/RenderInterface.h>

#include <nw/gfx/gfx.hpp>

#include <cstdint>

class RmlNwgfxRenderer final : public Rml::RenderInterface {
public:
    ~RmlNwgfxRenderer() override;

    bool initialize(nw::gfx::Core* core, nw::gfx::Context* context);
    void shutdown();

    bool begin_frame();
    void end_frame();
    void finish_render_pass();
    void on_resize(uint32_t width, uint32_t height, Rml::Context* context);

    bool is_frame_active() const { return frame_active_; }
    nw::gfx::CommandList* command_list() const noexcept { return command_list_; }
    void set_generated_textures(
        const std::vector<nw::toolset::RmlGeneratedTexture>* textures) noexcept
    {
        generated_textures_ = textures;
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture_handle) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    void render_stencil_mask_if_needed();
    struct Impl;

    nw::gfx::Core* core_ = nullptr;
    nw::gfx::Context* context_ = nullptr;
    nw::gfx::CommandList* command_list_ = nullptr;
    nw::gfx::FrameInfo frame_info_{};
    Impl* impl_ = nullptr;

    bool initialized_ = false;
    bool frame_active_ = false;
    bool warned_not_rendering_ = false;
    const std::vector<nw::toolset::RmlGeneratedTexture>* generated_textures_ = nullptr;
};
