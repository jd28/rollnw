// Batched static shadow-map pixel shader.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

#include "static_shadow_draw_data.inc.hlsl"

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint draw_id : TEXCOORD1;
};

void main(PSInput input) {
    StaticShadowDrawData draw = g_static_shadow_draws[input.draw_id];
    if (draw.texture_flags.y != 0) {
        const uint texture_index = draw.texture_flags.x;
        const float alpha = g_textures[NonUniformResourceIndex(texture_index)].Sample(g_sampler, input.texcoord).a;
        clip(alpha - draw.alpha_params.x);
    }
}
