// Batched static shadow-map vertex shader.

#include "scene_constants.inc.hlsl"
#include "static_shadow_draw_data.inc.hlsl"

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint draw_id : TEXCOORD1;
};

VSOutput main(VSInput input, uint instance_id : SV_InstanceID) {
    VSOutput output;
    StaticShadowDrawData draw = g_static_shadow_draws[instance_id];
    float4 world_pos = mul(draw.model, float4(input.position, 1.0));
    output.position = mul(projection, mul(view, world_pos));
    output.texcoord = input.texcoord;
    output.draw_id = instance_id;
    return output;
}
