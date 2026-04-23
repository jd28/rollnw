Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

cbuffer FogCompositeConstants : register(b1) {
    uint color_texture_index;
    uint depth_texture_index;
    float fog_amount;
    float _pad0;
    float4 fog_color;
    float2 fog_range;
    float2 camera_clip_planes;
};

struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float reconstruct_view_distance(float depth)
{
    const float near_plane = camera_clip_planes.x;
    const float far_plane = camera_clip_planes.y;
    return (near_plane * far_plane) / max(far_plane - depth * (far_plane - near_plane), 1.0e-4);
}

float4 main(PSInput input) : SV_Target
{
    const uint color_idx = NonUniformResourceIndex(color_texture_index);
    const uint depth_idx = NonUniformResourceIndex(depth_texture_index);

    float4 scene_color = g_textures[color_idx].SampleLevel(g_sampler, input.texcoord, 0.0);
    uint depth_width = 0;
    uint depth_height = 0;
    g_textures[depth_idx].GetDimensions(depth_width, depth_height);
    int2 depth_pixel = min(int2(input.texcoord * float2(depth_width, depth_height)), int2(depth_width - 1, depth_height - 1));
    float depth = g_textures[depth_idx].Load(int3(depth_pixel, 0)).x;
    if (depth >= 0.99999) {
        return scene_color;
    }

    const float fog_start = min(fog_range.x, fog_range.y);
    const float fog_end = max(fog_range.x, fog_range.y);
    const float scene_distance = reconstruct_view_distance(depth);
    const float ramp = saturate((scene_distance - fog_start) / max(fog_end - fog_start, 1.0e-4));
    const float shaped = 1.0 - pow(1.0 - ramp, 1.0 + saturate(fog_amount) * 3.0);
    scene_color.rgb = lerp(scene_color.rgb, fog_color.rgb, shaped);
    return scene_color;
}
