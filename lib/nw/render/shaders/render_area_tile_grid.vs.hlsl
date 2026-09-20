cbuffer AreaTileGridConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4 viewport_line;
};

struct VSInput {
    float3 position  : POSITION;
    float3 opposite  : TEXCOORD0;
    float2 extrusion : TEXCOORD1;
    float4 color     : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 clip = mul(projection, mul(view, float4(input.position, 1.0)));
    const float4 opposite_clip
        = mul(projection, mul(view, float4(input.opposite, 1.0)));
    const float2 viewport = max(viewport_line.xy, float2(1.0, 1.0));
    const float clip_w = max(abs(clip.w), 1.0e-6);
    const float opposite_w = max(abs(opposite_clip.w), 1.0e-6);
    const float2 delta = opposite_clip.xy / opposite_w - clip.xy / clip_w;
    const float delta_length_squared = dot(delta, delta);
    const float2 perpendicular = delta_length_squared > 1.0e-12
        ? float2(-delta.y, delta.x) * rsqrt(delta_length_squared)
        : float2(0.0, 0.0);
    const float half_width_pixels = max(viewport_line.z, 0.5) * 0.5;
    const float2 ndc_offset
        = perpendicular * input.extrusion.x * half_width_pixels * 2.0 / viewport;
    clip.xy += ndc_offset * clip.w;
    clip.z -= max(viewport_line.w, 0.0) * clip.w;
    output.position = clip;
    output.color = input.color;
    return output;
}
