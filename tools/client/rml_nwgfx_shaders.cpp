#include "rml_nwgfx_shaders.hpp"

#ifdef NW_GFX_BACKEND_METAL

namespace {

// Matches RmlUi_Vulkan/shader_vert.vert, shader_frag_color.frag, and
// shader_frag_texture.frag with Vulkan->Metal clip-space Y inversion.
constexpr char kMetalVertShader[] = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct UserData {
    float4x4 m_transform;
    float2 m_translate;
};

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color0 [[attribute(1)]];
    float2 texcoord0 [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 fragTexCoord;
    float4 fragColor;
};

vertex VertexOut main_0(VertexIn in [[stage_in]], constant UserData& userdata [[buffer(1)]])
{
    VertexOut out;
    out.fragTexCoord = in.texcoord0;
    out.fragColor = in.color0;
    float2 translatedPos = in.position + userdata.m_translate;
    float4 outPos = userdata.m_transform * float4(translatedPos, 0.0, 1.0);
    outPos.y = -outPos.y;
    out.position = outPos;
    return out;
}
)MSL";

constexpr char kMetalFragColorShader[] = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 fragTexCoord;
    float4 fragColor;
};

fragment float4 main_0(VertexOut in [[stage_in]])
{
    return in.fragColor;
}
)MSL";

constexpr char kMetalFragTextureShader[] = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 fragTexCoord;
    float4 fragColor;
};

fragment float4 main_0(
    VertexOut in [[stage_in]],
    texture2d<float, access::sample> tex [[texture(0)]],
    sampler samp [[sampler(0)]])
{
    float4 texColor = tex.sample(samp, in.fragTexCoord);
    return in.fragColor * texColor;
}
)MSL";

} // namespace

RmlNwgfxShaderBytes rml_nwgfx_vertex_shader() noexcept
{
    return {kMetalVertShader, sizeof(kMetalVertShader) - 1};
}

RmlNwgfxShaderBytes rml_nwgfx_color_fragment_shader() noexcept
{
    return {kMetalFragColorShader, sizeof(kMetalFragColorShader) - 1};
}

RmlNwgfxShaderBytes rml_nwgfx_texture_fragment_shader() noexcept
{
    return {kMetalFragTextureShader, sizeof(kMetalFragTextureShader) - 1};
}

#else

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

#endif
