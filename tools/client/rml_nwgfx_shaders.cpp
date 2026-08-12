#include "rml_nwgfx_shaders.hpp"

// Keep RmlUi's stock bytecode dependency isolated from the renderer backend.
// The renderer owns nw::gfx objects only; backend-specific shader validation stays in nw::gfx.
#include <RmlUi_Vulkan/ShadersCompiledSPV.h>

RmlNwgfxShaderBytes rml_nwgfx_vertex_shader() noexcept
{
    return {shader_vert, sizeof(shader_vert)};
}

RmlNwgfxShaderBytes rml_nwgfx_color_fragment_shader() noexcept
{
    return {shader_frag_color, sizeof(shader_frag_color)};
}

RmlNwgfxShaderBytes rml_nwgfx_texture_fragment_shader() noexcept
{
    return {shader_frag_texture, sizeof(shader_frag_texture)};
}
