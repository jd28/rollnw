cbuffer FogCompositeConstants : register(b1) {
    uint color_texture_index;
    uint depth_texture_index;
    float fog_amount;
    float _pad0;
    float4 fog_color;
    float2 fog_range;
    float2 camera_clip_planes;
};

struct VSInput {
    float2 position : POSITION;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.texcoord = input.texcoord;
    return output;
}
