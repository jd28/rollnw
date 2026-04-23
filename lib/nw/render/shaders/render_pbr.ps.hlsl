// PBR pixel shader — 3-point studio lighting, Blinn-Phong specular.
// SceneConstants layout must match SceneConstants in renderer.hpp exactly.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);
ByteAddressBuffer g_plt_palette : register(t2, space0);

cbuffer SceneConstants : register(b1) {
    float4x4 view;
    float4x4 projection;
    float4x4 model;
    float4x4 normal_matrix;
    uint texture_index;
    uint alpha_cutout;
    float alpha_cutout_threshold;
    float _pad_tex;
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
};

cbuffer ShadowConstants : register(b4) {
    float4x4 world_to_shadow[3];
    uint4 shadow_texture_indices;
    float4 shadow_split_distances;
    float shadow_strength;
    uint debug_mode;
    float2 _shadow_pad;
};

struct PSInput {
    float4 position  : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texcoord  : TEXCOORD2;
    float3 view_dir  : TEXCOORD3;
};

// Simple Lambert diffuse
float3 lambert(float3 N, float3 L, float3 color) {
    return color * max(dot(N, L), 0.0);
}

// Legacy NWN assets are mostly diffuse-driven, so keep the specular lobe broad and subtle.
float3 blinn_phong(float3 N, float3 L, float3 V, float3 color, float roughness, float strength) {
    float3 H = normalize(L + V);
    float spec_power = (1.0 - roughness) * 48.0 + 4.0;
    float spec = pow(max(dot(N, H), 0.0), spec_power);
    return color * spec * (1.0 - roughness) * strength;
}

uint plt_selected_color(uint layer) {
    switch (layer) {
    case 0: return plt_colors0.x;
    case 1: return plt_colors0.y;
    case 2: return plt_colors0.z;
    case 3: return plt_colors0.w;
    case 4: return plt_colors1.x;
    case 5: return plt_colors1.y;
    case 6: return plt_colors1.z;
    case 7: return plt_colors1.w;
    case 8: return plt_colors2.x;
    case 9: return plt_colors2.y;
    default: return 0;
    }
}

float4 unpack_rgba8(uint value) {
    return float4(
        float(value & 0xffu),
        float((value >> 8u) & 0xffu),
        float((value >> 16u) & 0xffu),
        float((value >> 24u) & 0xffu)) / 255.0;
}

float4 sample_plt(float2 texcoord) {
    uint tex_idx = NonUniformResourceIndex(texture_index);
    uint width = 0;
    uint height = 0;
    g_textures[tex_idx].GetDimensions(width, height);
    if (width == 0 || height == 0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    uint2 pixel = min(uint2(texcoord * float2(width, height)), uint2(width - 1, height - 1));
    float4 raw = g_textures[tex_idx].Load(int3(pixel, 0));
    uint color = (uint)round(saturate(raw.x) * 255.0);
    uint layer = (uint)round(saturate(raw.y) * 255.0);
    if (color == 255u || layer >= 10u) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    uint meta_base = layer * 16u;
    uint offset = g_plt_palette.Load(meta_base + 0u);
    uint palette_width = g_plt_palette.Load(meta_base + 4u);
    uint palette_height = g_plt_palette.Load(meta_base + 8u);
    uint row = plt_selected_color(layer);
    if (row >= palette_height || color >= palette_width) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    uint byte_address = (40u + offset + row * palette_width + color) * 4u;
    return unpack_rgba8(g_plt_palette.Load(byte_address));
}

float sample_shadow_map(uint texture_index, float2 shadow_uv, float receiver_depth, float bias) {
    uint2 shadow_size = 0;
    g_textures[NonUniformResourceIndex(texture_index)].GetDimensions(shadow_size.x, shadow_size.y);
    if (shadow_size.x == 0 || shadow_size.y == 0) {
        return 1.0;
    }

    float2 texel = 1.0 / float2(shadow_size);
    float visibility = 0.0;
    [unroll]
    for (int y = 0; y < 2; ++y) {
        [unroll]
        for (int x = 0; x < 2; ++x) {
            float2 uv = saturate(shadow_uv + (float2(x, y) - 0.5) * texel);
            int2 pixel = min(int2(uv * float2(shadow_size)), int2(shadow_size) - 1);
            float shadow_depth = g_textures[NonUniformResourceIndex(texture_index)].Load(int3(pixel, 0)).x;
            visibility += (receiver_depth - bias) <= shadow_depth ? 1.0 : 0.0;
        }
    }
    return visibility * 0.25;
}

float shadow_visibility(float3 world_pos, float3 N, float3 L, float scene_distance) {
    uint cascade = 0;
    if (scene_distance > shadow_split_distances.x) cascade = 1;
    if (scene_distance > shadow_split_distances.y) cascade = 2;

    const uint shadow_texture_index = shadow_texture_indices[cascade];
    if (shadow_texture_index == 0) {
        return 1.0;
    }

    float4 shadow_clip = mul(world_to_shadow[cascade], float4(world_pos, 1.0));
    if (shadow_clip.w <= 0.0) {
        return 1.0;
    }

    float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    float2 shadow_uv = shadow_ndc.xy * 0.5 + 0.5;
    if (any(shadow_uv < 0.0.xx) || any(shadow_uv > 1.0.xx) || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0) {
        return 1.0;
    }

    float bias = max(0.00035, (0.0015 + 0.0008 * cascade) * (1.0 - saturate(dot(N, L))) + 0.0002 * cascade);
    float visibility = sample_shadow_map(shadow_texture_index, shadow_uv, shadow_ndc.z, bias);
    return lerp(1.0 - shadow_strength, 1.0, visibility);
}

float3 cascade_debug_color(uint cascade) {
    switch (cascade) {
    case 0: return float3(1.0, 0.25, 0.25);
    case 1: return float3(0.25, 1.0, 0.35);
    default: return float3(0.25, 0.55, 1.0);
    }
}

float4 main(PSInput input) : SV_Target {
    float3 N = normalize(input.normal);
    float3 V = normalize(input.view_dir);

    // Sample diffuse texture; fallback color if no texture (1×1 white → no change)
    float4 texel = plt_enabled != 0 ? sample_plt(input.texcoord) : g_textures[NonUniformResourceIndex(texture_index)].Sample(g_sampler, input.texcoord);
    if (alpha_cutout != 0) {
        float3 color_key = float3(color_key_rg, color_key_b);
        if (color_key_threshold > 0.0) {
            float3 delta = texel.rgb - color_key;
            clip(dot(delta, delta) - (color_key_threshold * color_key_threshold));
        }
        clip(texel.a - alpha_cutout_threshold);
        texel.a = 1.0;
    }
    float3 albedo = texel.rgb;
    float  roughness = 0.78;
    float  specular_strength = 0.12;
    float scene_distance = length(input.view_dir);

    float3 Lo = float3(0.0, 0.0, 0.0);

    // Key light
    float3 L_key = normalize(-key_dir_intensity.xyz);
    uint cascade = 0;
    if (scene_distance > shadow_split_distances.x) cascade = 1;
    if (scene_distance > shadow_split_distances.y) cascade = 2;
    float key_shadow = shadow_visibility(input.world_pos, N, L_key, scene_distance);
    Lo += lambert(N, L_key, key_color.xyz) * key_dir_intensity.w * key_shadow;
    Lo += blinn_phong(N, L_key, V, key_color.xyz, roughness, specular_strength) * key_dir_intensity.w * key_shadow;

    // Fill light
    float3 L_fill = normalize(-fill_dir_intensity.xyz);
    Lo += lambert(N, L_fill, fill_color.xyz) * fill_dir_intensity.w;

    // Rim light
    float3 L_rim = normalize(-rim_dir_intensity.xyz);
    float rim_factor = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    Lo += rim_color.xyz * rim_factor * rim_dir_intensity.w * max(dot(N, L_rim), 0.0);

    float3 final_color = albedo * ambient.xyz + albedo * Lo;
    float output_alpha = (alpha_cutout != 0 ? 1.0 : texel.a) * pad_alpha.x;
    final_color *= pad_alpha.x;

    if (fog_enabled != 0) {
        const float fog_start = min(fog_range.x, fog_range.y);
        const float fog_end = max(fog_range.x, fog_range.y);
        const float fog_distance = max(scene_distance - fog_start, 0.0);
        const float fog_range_span = max(fog_end - fog_start, 1.0e-4);
        const float fog_normalized = fog_distance / fog_range_span;
        const float fog_density = lerp(2.5, 8.0, saturate(fog_amount));
        const float fog_extinction = pow(fog_normalized, 1.35);
        const float transmittance = exp(-fog_density * fog_extinction);
        const float fog_factor = 1.0 - saturate(transmittance);

        if (alpha_cutout == 0 && output_alpha < 0.999) {
            const float transparent_fog = saturate(fog_factor * (1.35 + (1.0 - output_alpha) * 1.5));
            const float visibility = 1.0 - transparent_fog;
            final_color *= visibility;
            output_alpha *= visibility;
        } else {
            final_color = final_color * transmittance + fog_color.rgb * (1.0 - transmittance);
        }
    }

    // Reinhard tone mapping
    final_color = final_color / (final_color + float3(1.0, 1.0, 1.0));

    // Present to a non-sRGB swapchain path; encode back to display gamma.
    final_color = pow(saturate(final_color), 1.0 / 2.2);

    if (debug_mode != 0) {
        float3 debug_color = cascade_debug_color(cascade);
        final_color = lerp(final_color, debug_color, 0.65);
    }

    return float4(final_color, output_alpha);
}
