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

struct VSInput {
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR0;
};

struct VSOutput {
    float4 position      : SV_Position;
    float2 texcoord      : TEXCOORD0;
    float4 color         : COLOR0;
    float view_distance  : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world = float4(input.position, 1.0);
    float4 view_pos = mul(view, world);
    output.position = mul(projection, view_pos);
    output.texcoord = input.texcoord;
    output.color = input.color;
    output.view_distance = length(view_pos.xyz);
    return output;
}
