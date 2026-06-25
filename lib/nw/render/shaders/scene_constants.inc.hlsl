// Shared per-draw scene constants.
// Layout must match SceneConstants in lib/nw/render/render_context.hpp exactly.

cbuffer SceneConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4x4 model;
    float4x4 normal_matrix;   // inverse-transpose of model
    uint texture_index;
    uint alpha_cutout;
    float alpha_cutout_threshold;
    uint two_sided_lighting;
    float2 color_key_rg;
    float color_key_b;
    float color_key_threshold;
    float4 pad_alpha;
    float4 camera_pos;
    float4 ambient;
    float4 key_dir_intensity;
    float4 key_color;
    float4 fill_dir_intensity;
    float4 fill_color;
    float4 rim_dir_intensity;
    float4 rim_color;
    float4 fog_color;
    float2 fog_range;
    float fog_amount;
    uint fog_enabled;
    uint plt_enabled;
    uint ibl_diffuse_texture_index;
    uint ibl_specular_texture_index;
    uint ibl_brdf_lut_texture_index;
    uint4 plt_colors0;
    uint4 plt_colors1;
    uint4 plt_colors2;
    float4 environment_wind;
    float4 environment_weather;
    uint4 forward_plus_cluster_dims;
    uint4 forward_plus_params;
    float4 forward_plus_depth_params;
    float4 forward_plus_viewport;
    float4 emissive_color;
    float4 material_params;
    uint4 material_texture_indices;
    float4x4 scene_shadow_world_to_shadow[3];
    uint4 scene_shadow_texture_indices;
    float4 scene_shadow_split_distances;
    float scene_shadow_strength;
    uint scene_shadow_debug_mode;
    float2 scene_shadow_pad;
    float4x4 scene_local_shadow_world_to_shadow[4];
    uint4 scene_local_shadow_texture_indices;
    float4 scene_local_shadow_params;
};

// Normalized wind direction from environment_wind, with a stable fallback
// when wind is effectively zero.
float2 safe_wind_direction() {
    const float2 wind = environment_wind.xy;
    const float wind_len_sq = dot(wind, wind);
    return wind_len_sq > 1.0e-4 ? wind * rsqrt(wind_len_sq) : float2(0.70710677, 0.70710677);
}
