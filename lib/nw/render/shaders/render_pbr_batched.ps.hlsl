// Batched PBR pixel shader for NWN static meshes.
// Matches render_pbr.ps.hlsl behavior, with per-draw fields read from g_static_draws.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

#include "scene_constants.inc.hlsl"
#include "plt_palette.inc.hlsl"
#include "forward_plus.inc.hlsl"
#include "static_draw_data.inc.hlsl"
#include "shadow.inc.hlsl"

struct PSInput {
    float4 position  : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texcoord  : TEXCOORD2;
    float3 view_dir  : TEXCOORD3;
    float3 tangent   : TEXCOORD4;
    float3 bitangent : TEXCOORD5;
    nointerpolation uint draw_id : TEXCOORD6;
    float view_depth  : TEXCOORD7;
};

float3 lambert(float3 N, float3 L, float3 color) {
    return color * max(dot(N, L), 0.0);
}

float3 blinn_phong(float3 N, float3 L, float3 V, float3 color, float roughness, float strength) {
    float3 H = normalize(L + V);
    float spec_power = (1.0 - roughness) * 48.0 + 4.0;
    float spec = pow(max(dot(N, H), 0.0), spec_power);
    return color * spec * (1.0 - roughness) * strength;
}

float3 two_sided_lighting_normal(float3 N, float3 V, uint enabled) {
    if (enabled == 0 || dot(N, V) >= 0.0) {
        return N;
    }
    return -N;
}

float3 material_normal(float3 geom_N, float3 T, float3 B, float2 texcoord, uint normal_index) {
    if (normal_index == 0u) {
        return geom_N;
    }

    float3 normal_sample = g_textures[NonUniformResourceIndex(normal_index)].Sample(g_sampler, texcoord).xyz;
    normal_sample = normal_sample * 2.0 - 1.0;
    normal_sample.z = sqrt(saturate(1.0 - dot(normal_sample.xy, normal_sample.xy)));
    return normalize(normal_sample.x * T + normal_sample.y * B + normal_sample.z * geom_N);
}

float material_roughness(float roughness, float2 texcoord, uint surface_index) {
    if (surface_index == 0u) {
        return roughness;
    }

    float roughness_sample = g_textures[NonUniformResourceIndex(surface_index)].Sample(g_sampler, texcoord).g;
    return clamp(roughness * roughness_sample, 0.05, 1.0);
}

float material_specular_strength(float strength, float2 texcoord, uint specular_index) {
    if (specular_index == 0u) {
        return strength;
    }

    float3 specular_sample = g_textures[NonUniformResourceIndex(specular_index)].Sample(g_sampler, texcoord).rgb;
    return max(strength * max(specular_sample.r, max(specular_sample.g, specular_sample.b)), 0.0);
}

uint plt_selected_color(uint layer, uint4 colors0, uint4 colors1, uint4 colors2) {
    switch (layer) {
    case 0: return colors0.x;
    case 1: return colors0.y;
    case 2: return colors0.z;
    case 3: return colors0.w;
    case 4: return colors1.x;
    case 5: return colors1.y;
    case 6: return colors1.z;
    case 7: return colors1.w;
    case 8: return colors2.x;
    case 9: return colors2.y;
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

float3 srgb_to_linear(float3 color) {
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(low, high, step(float3(0.04045, 0.04045, 0.04045), color));
}

float4 sample_plt(float2 texcoord, uint texture_idx, uint4 colors0, uint4 colors1, uint4 colors2) {
    uint tex_idx = NonUniformResourceIndex(texture_idx);
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

    uint palette_width = plt_palette_width(layer);
    uint palette_height = plt_palette_height(layer);
    uint row = plt_selected_color(layer, colors0, colors1, colors2);
    if (row >= palette_height || color >= palette_width) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    float4 palette_color = unpack_rgba8(plt_palette_color(layer, row, color));
    palette_color.rgb = srgb_to_linear(palette_color.rgb);
    return palette_color;
}

float shadow_visibility(float3 world_pos, float3 N, float3 L, float scene_distance) {
    float visibility = shadow_directional_raw_visibility(
        world_pos, N, L, scene_distance,
        0.0015, 0.0008, 0.00035, 0.0002,
        0.045, 0.030,
        1.15, 0.45);
    return lerp(1.0 - shadow_strength, 1.0, visibility);
}

float3 cascade_debug_color(uint cascade) {
    switch (cascade) {
    case 0: return float3(1.0, 0.25, 0.25);
    case 1: return float3(0.25, 1.0, 0.35);
    default: return float3(0.25, 0.55, 1.0);
    }
}

void accumulate_local_light(
    inout float3 Lo,
    float3 world_pos,
    float3 N,
    float3 V,
    float roughness,
    float specular_strength,
    float4 pos_radius,
    float4 color_intensity,
    float4 light_params)
{
    bool ambient_local_light = light_params.x >= 0.5;
    float radius = max(pos_radius.w, 1.0e-3);
    float3 light_delta = pos_radius.xyz - world_pos;
    if (ambient_local_light) {
        light_delta.z *= saturate(light_params.y);
    }
    float dist_sq = dot(light_delta, light_delta);
    float radius_sq = radius * radius;
    if (dist_sq >= radius_sq) {
        return;
    }

    float distance = sqrt(max(dist_sq, 1.0e-5));
    float inv_dist = rcp(distance);
    float3 L_local = light_delta * inv_dist;
    float falloff = saturate(dist_sq / radius_sq);
    float attenuation = saturate(1.0 - falloff);
    attenuation *= attenuation;
    float intensity = color_intensity.w * attenuation;
    // Shadow only this light's own contribution (params.z is shadow slot + 1).
    // Applies to ambient-contribution lights too — in NWN interiors that path
    // carries almost all local illumination, so it must be shadowable for an
    // occluder to cast anything. Still per-light, so other lights fill in (no aura).
    uint shadow_slot_plus_one = (uint)(light_params.z + 0.5);
    if (shadow_slot_plus_one > 0) {
        intensity *= local_shadow_visibility(shadow_slot_plus_one - 1, world_pos, N, L_local);
    }
    if (ambient_local_light) {
        Lo += color_intensity.xyz * intensity;
        return;
    }
    float legacy_tile_diffuse = saturate(dot(N, L_local) * 0.45 + 0.55);
    Lo += color_intensity.xyz * legacy_tile_diffuse * intensity;
    Lo += blinn_phong(N, L_local, V, color_intensity.xyz, roughness, specular_strength * 0.55) * intensity;
}

float4 main(PSInput input) : SV_Target {
    StaticDrawData draw = g_static_draws[input.draw_id];
    const uint draw_texture_index = draw.texture_flags.x;
    const uint draw_alpha_cutout = draw.texture_flags.y;
    const uint draw_plt_enabled = draw.texture_flags.z;
    const uint draw_two_sided_lighting = draw.texture_flags.w;

    float3 V = normalize(input.view_dir);
    float3 geom_N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    if (draw_two_sided_lighting != 0 && dot(geom_N, V) < 0.0) {
        geom_N = -geom_N;
        T = -T;
        B = -B;
    }
    float3 N = material_normal(geom_N, T, B, input.texcoord, draw.material_texture_indices.x);

    float4 texel = draw_plt_enabled != 0
        ? sample_plt(input.texcoord, draw_texture_index, draw.plt_colors0, draw.plt_colors1, draw.plt_colors2)
        : g_textures[NonUniformResourceIndex(draw_texture_index)].Sample(g_sampler, input.texcoord);
    if (draw_alpha_cutout != 0) {
        float3 color_key = draw.color_key_threshold.xyz;
        const float color_key_cutoff = draw.color_key_threshold.w;
        if (color_key_cutoff > 0.0) {
            float3 delta = texel.rgb - color_key;
            clip(dot(delta, delta) - (color_key_cutoff * color_key_cutoff));
        }
        clip(texel.a - draw.alpha_params.x);
        texel.a = 1.0;
    }
    float3 albedo = texel.rgb;
    float roughness = clamp(draw.material_params.x, 0.05, 1.0);
    float specular_strength = max(draw.material_params.y, 0.0);
    roughness = material_roughness(roughness, input.texcoord, draw.material_texture_indices.y);
    specular_strength = material_specular_strength(specular_strength, input.texcoord, draw.material_texture_indices.w);
    float scene_distance = length(input.view_dir);

    float3 Lo = float3(0.0, 0.0, 0.0);

    float3 L_key = normalize(-key_dir_intensity.xyz);
    uint cascade = shadow_cascade_index(scene_distance);
    float key_shadow = shadow_visibility(input.world_pos, N, L_key, scene_distance);
    Lo += lambert(N, L_key, key_color.xyz) * key_dir_intensity.w * key_shadow;
    Lo += blinn_phong(N, L_key, V, key_color.xyz, roughness, specular_strength) * key_dir_intensity.w * key_shadow;

    float3 L_fill = normalize(-fill_dir_intensity.xyz);
    Lo += lambert(N, L_fill, fill_color.xyz) * fill_dir_intensity.w;

    float3 L_rim = normalize(-rim_dir_intensity.xyz);
    float rim_factor = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    Lo += rim_color.xyz * rim_factor * rim_dir_intensity.w * max(dot(N, L_rim), 0.0);

    if (forward_plus_enabled()) {
        const ForwardPlusLightRange light_range = forward_plus_light_range(input.position, input.view_depth);
        [loop]
        for (uint i = 0; i < light_range.count; ++i) {
            ForwardPlusLightData light;
            if (!forward_plus_load_light(light_range, i, light)) {
                continue;
            }
            accumulate_local_light(
                Lo, input.world_pos, N, V, roughness, specular_strength,
                light.position_radius, light.color_intensity, light.params);
        }
    }

    float3 final_color = albedo * ambient.xyz + albedo * Lo;
    if (draw.material_texture_indices.z != 0u) {
        final_color += g_textures[NonUniformResourceIndex(draw.material_texture_indices.z)].Sample(g_sampler, input.texcoord).rgb
            * draw.emissive_color.rgb;
    } else {
        final_color += albedo * draw.emissive_color.rgb;
    }
    float output_alpha = (draw_alpha_cutout != 0 ? 1.0 : texel.a) * draw.pad_alpha.x;
    final_color *= draw.pad_alpha.x;

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

        if (draw_alpha_cutout == 0 && output_alpha < 0.999) {
            const float transparent_fog = saturate(fog_factor * (1.35 + (1.0 - output_alpha) * 1.5));
            const float visibility = 1.0 - transparent_fog;
            final_color *= visibility;
            output_alpha *= visibility;
        } else {
            final_color = final_color * transmittance + fog_color.rgb * (1.0 - transmittance);
        }
    }

    final_color = final_color / (final_color + float3(1.0, 1.0, 1.0));
    final_color = pow(saturate(final_color), 1.0 / 2.2);

    if (debug_mode != 0) {
        float3 debug_color = cascade_debug_color(cascade);
        final_color = lerp(final_color, debug_color, 0.65);
    }
    final_color = forward_plus_apply_debug(final_color, input.position, input.view_depth, 0.72);

    return float4(final_color, output_alpha);
}
