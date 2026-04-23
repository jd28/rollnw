cbuffer DebugGridConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4 minor_color;
    float4 major_color;
    float4 grid_params;
};

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 world    : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world = float4(input.position, 1.0);
    output.position = mul(projection, mul(view, world));
    output.world = world.xyz;
    return output;
}
