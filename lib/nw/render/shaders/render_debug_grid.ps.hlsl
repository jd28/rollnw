struct PSInput {
    float4 position : SV_Position;
    float3 world    : TEXCOORD0;
};

cbuffer DebugGridConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4 minor_color;
    float4 major_color;
    float4 grid_params;
};

float pristine_grid(float2 uv, float line_width)
{
    float2 width = float2(line_width, line_width);
    float4 uv_ddxy = float4(ddx(uv), ddy(uv));
    float2 uv_deriv = float2(length(uv_ddxy.xz), length(uv_ddxy.yw));
    bool2 invert_line = width > 0.5;
    float2 invert_mask = float2(invert_line.x ? 1.0 : 0.0, invert_line.y ? 1.0 : 0.0);
    float2 target_width = lerp(width, 1.0 - width, invert_mask);
    float2 draw_width = clamp(target_width, uv_deriv, 0.5);
    float2 line_aa = max(uv_deriv, 1.0e-6) * 1.5;
    float2 grid_uv = abs(frac(uv) * 2.0 - 1.0);
    grid_uv = lerp(1.0 - grid_uv, grid_uv, invert_mask);
    float2 grid2 = smoothstep(draw_width + line_aa, draw_width - line_aa, grid_uv);
    grid2 *= saturate(target_width / draw_width);
    grid2 = lerp(grid2, target_width, saturate(uv_deriv * 2.0 - 1.0));
    grid2 = lerp(grid2, 1.0 - grid2, invert_mask);
    return lerp(grid2.x, 1.0, grid2.y);
}

float4 main(PSInput input) : SV_Target
{
    float spacing = max(grid_params.x, 1.0e-4);
    float major_interval = max(grid_params.y, 1.0);
    float minor_width = saturate(grid_params.z);
    float major_width = saturate(grid_params.w);

    float2 minor_uv = input.world.xy / spacing;
    float2 major_uv = minor_uv / major_interval;

    float minor = pristine_grid(minor_uv, minor_width);
    float major = pristine_grid(major_uv, major_width);
    float minor_only = minor * (1.0 - major);

    float4 color = minor_color * minor_only + major_color * major;
    color.rgb *= color.a;
    return color;
}
