// Depth-only shadow pixel shader for common PBR model primitives.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

#include "model_draw_constants.inc.hlsl"

struct PSInput {
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 normal     : TEXCOORD1;
    float2 texcoord   : TEXCOORD2;
    float3 view_dir   : TEXCOORD3;
    float3 tangent    : TEXCOORD4;
    float3 bitangent  : TEXCOORD5;
    float view_depth   : TEXCOORD6;
};

float4 sample_albedo(float2 texcoord) {
    if (sc_plt_enabled == 0u) {
        return g_textures[NonUniformResourceIndex(sc_albedo_index)].Sample(g_sampler, texcoord);
    }

    uint tex_idx = NonUniformResourceIndex(sc_albedo_index);
    uint width = 0;
    uint height = 0;
    g_textures[tex_idx].GetDimensions(width, height);
    if (width == 0 || height == 0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    uint2 pixel = min(uint2(texcoord * float2(width, height)), uint2(width - 1, height - 1));
    return g_textures[tex_idx].Load(int3(pixel, 0));
}

void main(PSInput input) {
    if (sc_alpha_mode == 1u) {
        const float4 albedo_sample = sample_albedo(input.texcoord);
        const float color_key_cutoff = sc_color_key_threshold.w;
        if (color_key_cutoff > 0.0) {
            float3 delta = albedo_sample.rgb - sc_color_key_threshold.xyz;
            clip(dot(delta, delta) - (color_key_cutoff * color_key_cutoff));
        }
        clip(albedo_sample.a - sc_alpha_cutoff);
    }
}
