// Static shadow-map vertex shader.

#include "scene_constants.inc.hlsl"

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.position = mul(projection, mul(view, world_pos));
    output.texcoord = input.texcoord;
    return output;
}
