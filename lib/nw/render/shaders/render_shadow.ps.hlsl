Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

#include "scene_constants.inc.hlsl"

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

void main(PSInput input) {
    if (alpha_cutout != 0) {
        float alpha = g_textures[NonUniformResourceIndex(texture_index)].Sample(g_sampler, input.texcoord).a;
        clip(alpha - alpha_cutout_threshold);
    }
}
