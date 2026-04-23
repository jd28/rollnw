cbuffer ParticleDrawConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    uint texture_index;
    uint alpha_cutout;
    uint additive_like;
    uint additive_alpha_gated;
    float alpha_cutout_threshold;
    float additive_intensity;
    float alpha_intensity;
    float4 fog_color;
    float2 fog_range;
    float fog_amount;
    uint fog_enabled;
};

struct ParticleBillboardInstance {
    float4 center;
    float4 right;
    float4 up;
    float4 texcoord_rect;
    float4 color;
};

StructuredBuffer<ParticleBillboardInstance> g_instances : register(t2, space0);

struct VSInput {
    float2 corner   : POSITION;
    float2 texcoord : TEXCOORD0;
    uint instance_id : SV_InstanceID;
};

struct VSOutput {
    float4 position      : SV_Position;
    float2 texcoord      : TEXCOORD0;
    float4 color         : COLOR0;
    float view_distance  : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    ParticleBillboardInstance instance = g_instances[input.instance_id];
    float3 world_position = instance.center.xyz
        + instance.right.xyz * input.corner.x
        + instance.up.xyz * input.corner.y;
    float2 uv = lerp(instance.texcoord_rect.xy, instance.texcoord_rect.zw, input.texcoord);

    VSOutput output;
    float4 world = float4(world_position, 1.0);
    float4 view_pos = mul(view, world);
    output.position = mul(projection, view_pos);
    output.texcoord = uv;
    output.color = instance.color;
    output.view_distance = length(view_pos.xyz);
    return output;
}
