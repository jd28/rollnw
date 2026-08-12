#pragma once

#include <cstddef>

struct RmlNwgfxShaderBytes {
    const void* data = nullptr;
    size_t size = 0;
};

RmlNwgfxShaderBytes rml_nwgfx_vertex_shader() noexcept;
RmlNwgfxShaderBytes rml_nwgfx_color_fragment_shader() noexcept;
RmlNwgfxShaderBytes rml_nwgfx_texture_fragment_shader() noexcept;
