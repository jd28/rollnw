// PBR pixel shader — GGX Cook-Torrance, 3-point studio lighting.
// SurfaceConstants layout must match nw::render::SurfaceConstants exactly.

Texture2D<float4> g_textures[] : register(t2, space1);
SamplerState g_sampler : register(s3, space1);

#include "scene_constants.inc.hlsl"
#include "plt_palette.inc.hlsl"
#include "forward_plus.inc.hlsl"
#include "scene_shadow.inc.hlsl"

cbuffer SurfaceConstants : register(b4) {
    float4 sc_albedo;
    float  sc_roughness;
    float  sc_metallic;
    float  sc_specular_strength;
    float  sc_normal_scale;
    float  sc_occlusion_strength;
    float  sc_ibl_strength;
    float  sc_exposure;
    float  sc_pad0;
    float4 sc_emissive;
    uint   sc_albedo_index;
    uint   sc_normal_index;
    uint   sc_surface_index;
    uint   sc_emissive_index;
    uint   sc_alpha_mode;
    float  sc_alpha_cutoff;
    uint   sc_double_sided;
    uint   sc_plt_enabled;
    uint4  sc_plt_colors0;
    uint4  sc_plt_colors1;
    uint4  sc_plt_colors2;
};

struct PSInput {
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 normal     : TEXCOORD1;
    float2 texcoord   : TEXCOORD2;
    float3 view_dir   : TEXCOORD3;
    float3 tangent    : TEXCOORD4;
    float3 bitangent  : TEXCOORD5;
    float view_depth   : TEXCOORD6;
};

static const float PI = 3.14159265358979;

float2 env_latlong_uv(float3 dir)
{
    dir = normalize(dir);
    float phi = atan2(dir.y, dir.x);
    float theta = acos(clamp(dir.z, -1.0, 1.0));
    return float2(phi / (2.0 * PI) + 0.5, theta / PI);
}

float3 fallback_env(float3 dir, float roughness)
{
    float hemi = dir.z * 0.5 + 0.5;
    float3 sky = float3(0.80, 0.86, 0.96);
    float3 ground = float3(0.08, 0.07, 0.06);
    float3 env = lerp(ground, sky, hemi);
    float gloss = 1.0 - roughness;
    env *= lerp(1.05, 0.22, roughness);
    env *= lerp(0.85, 1.35, gloss * gloss);
    return env;
}

float3 sample_env_specular(float3 dir, float roughness)
{
    if (ibl_specular_texture_index == 0u) {
        return fallback_env(dir, roughness);
    }

    uint width = 0;
    uint height = 0;
    uint levels = 1;
    g_textures[NonUniformResourceIndex(ibl_specular_texture_index)].GetDimensions(0, width, height, levels);
    float max_mip = max(float(levels) - 1.0, 0.0);
    float spec_mip = saturate(roughness) * max_mip;
    return g_textures[NonUniformResourceIndex(ibl_specular_texture_index)].SampleLevel(
        g_sampler, env_latlong_uv(dir), spec_mip).rgb;
}

float3 sample_env_diffuse(float3 dir)
{
    if (ibl_diffuse_texture_index == 0u) {
        float hemi = dir.z * 0.5 + 0.5;
        float3 sky = float3(0.34, 0.38, 0.46);
        float3 ground = float3(0.05, 0.05, 0.05);
        return lerp(ground, sky, hemi);
    }

    return g_textures[NonUniformResourceIndex(ibl_diffuse_texture_index)].SampleLevel(
        g_sampler, env_latlong_uv(dir), 0.0).rgb;
}

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float G_Smith(float NdotV, float NdotL, float a) {
    float k = (a + 1.0) * (a + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float VdotH, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

float2 env_brdf_ab_approx(float roughness, float NdotV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

float2 env_brdf_terms(float NdotV, float roughness)
{
    if (ibl_brdf_lut_texture_index == 0u) {
        return env_brdf_ab_approx(roughness, NdotV);
    }

    float2 brdf = g_textures[NonUniformResourceIndex(ibl_brdf_lut_texture_index)].SampleLevel(
        g_sampler, float2(saturate(NdotV), saturate(roughness)), 0.0).rg;
    return brdf.x >= 0.0 ? brdf : env_brdf_ab_approx(roughness, NdotV);
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
    if (palette_width == 0u || palette_height == 0u || row >= palette_height) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    color = min(color, palette_width - 1u);
    float4 palette_color = unpack_rgba8(plt_palette_color(layer, row, color));
    palette_color.rgb = srgb_to_linear(palette_color.rgb);
    return palette_color;
}

float3 rough_metal_energy_compensation(float3 F0, float roughness, float metallic, float2 brdf)
{
    float single_scatter_energy = saturate(brdf.x + brdf.y);
    float missing_energy = saturate(1.0 - single_scatter_energy);
    float rough_metal_weight = metallic * smoothstep(0.35, 1.0, roughness);
    float3 fresnel_average = F0 + (1.0 - F0) * (1.0 / 21.0);
    float3 denom = max(float3(1.0, 1.0, 1.0) - missing_energy * fresnel_average,
        float3(1.0e-4, 1.0e-4, 1.0e-4));
    return rough_metal_weight * (missing_energy * fresnel_average / denom);
}

float3 eval_env_diffuse(float3 N, float3 albedo_lin, float occlusion, float metallic) {
    float3 env = sample_env_diffuse(N);
    return env * albedo_lin * (1.0 - metallic) * occlusion * sc_ibl_strength;
}

float3 eval_env_specular(float3 N, float3 V, float3 F0, float roughness, float metallic, float occlusion) {
    float3 R = reflect(-V, N);
    float3 env = sample_env_specular(R, roughness);

    float NdotV = max(dot(N, V), 1e-4);
    float2 brdf = env_brdf_terms(NdotV, roughness);
    float3 single_scatter = F0 * brdf.x + brdf.y;
    float3 multi_scatter = rough_metal_energy_compensation(F0, roughness, metallic, brdf);
    return env * (single_scatter + multi_scatter) * occlusion * sc_ibl_strength;
}

float3 eval_light(float3 N, float3 V, float3 L,
                  float3 light_color, float intensity,
                  float3 albedo_lin, float3 F0, float a, float metallic) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return (float3)0;

    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float VdotH = max(dot(V, H), 0.0);

    float D = D_GGX(NdotH, a);
    float G = G_Smith(NdotV, NdotL, a);
    float3 F = F_Schlick(VdotH, F0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 kd = (1.0 - F) * (1.0 - metallic);

    return (kd * albedo_lin / PI + specular) * light_color * intensity * NdotL;
}

void accumulate_local_light_pbr(
    inout float3 Lo,
    float3 world_pos,
    float3 N,
    float3 V,
    float3 albedo_lin,
    float3 F0,
    float a,
    float metallic,
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

    float falloff = saturate(dist_sq / radius_sq);
    float attenuation = saturate(1.0 - falloff);
    attenuation *= attenuation;
    float intensity = color_intensity.w * attenuation;
    float3 L = light_delta * rsqrt(max(dist_sq, 1.0e-5));
    uint shadow_slot_plus_one = (uint)(light_params.z + 0.5);
    if (shadow_slot_plus_one > 0) {
        intensity *= scene_local_shadow_visibility(shadow_slot_plus_one - 1, world_pos, N, L);
    }
    if (ambient_local_light) {
        Lo += albedo_lin * color_intensity.xyz * intensity * (1.0 - metallic);
        return;
    }

    Lo += eval_light(N, V, L, color_intensity.xyz, intensity, albedo_lin, F0, a, metallic);
}

float3 tonemap_aces_fitted(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float4 main(PSInput input) : SV_Target {
    float3 V = normalize(input.view_dir);
    float3 geom_N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    if (sc_double_sided != 0u && dot(geom_N, V) < 0.0) {
        geom_N = -geom_N;
        T = -T;
        B = -B;
    }

    float3 normal_sample = g_textures[NonUniformResourceIndex(sc_normal_index)].Sample(g_sampler, input.texcoord).xyz;
    normal_sample = normal_sample * 2.0 - 1.0;
    normal_sample.xy *= sc_normal_scale;
    normal_sample.z = sqrt(saturate(1.0 - dot(normal_sample.xy, normal_sample.xy)));
    float3 N = normalize(normal_sample.x * T + normal_sample.y * B + normal_sample.z * geom_N);

    float4 albedo_sample = sc_plt_enabled != 0u
        ? sample_plt(input.texcoord, sc_albedo_index, sc_plt_colors0, sc_plt_colors1, sc_plt_colors2) * sc_albedo
        : g_textures[NonUniformResourceIndex(sc_albedo_index)].Sample(g_sampler, input.texcoord) * sc_albedo;
    if (sc_alpha_mode == 1u) clip(albedo_sample.a - sc_alpha_cutoff);
    float3 albedo_lin = albedo_sample.rgb;

    float4 surface_sample = g_textures[NonUniformResourceIndex(sc_surface_index)].Sample(g_sampler, input.texcoord);
    float occlusion = lerp(1.0, surface_sample.r, saturate(sc_occlusion_strength));
    float roughness = clamp(sc_roughness * surface_sample.g, 0.05, 1.0);
    float a = roughness * roughness;
    float m = sc_metallic * surface_sample.b;
    float3 emissive_sample = g_textures[NonUniformResourceIndex(sc_emissive_index)].Sample(g_sampler, input.texcoord).rgb;
    float scene_distance = length(input.view_dir);

    float dielectric_f0 = 0.04 * saturate(sc_specular_strength);
    float3 F0 = lerp(float3(dielectric_f0, dielectric_f0, dielectric_f0), albedo_lin, m);
    float3 Lo = (float3)0;
    float3 L_key = normalize(-key_dir_intensity.xyz);
    float key_shadow = scene_shadow_visibility(input.world_pos, N, L_key, scene_distance);
    Lo += eval_light(N, V, L_key, key_color.xyz, key_dir_intensity.w * key_shadow, albedo_lin, F0, a, m);
    Lo += eval_light(N, V, normalize(-fill_dir_intensity.xyz), fill_color.xyz, fill_dir_intensity.w, albedo_lin, F0, a, m);
    Lo += eval_light(N, V, normalize(-rim_dir_intensity.xyz), rim_color.xyz, rim_dir_intensity.w, albedo_lin, F0, a, m);

    if (forward_plus_enabled()) {
        const ForwardPlusLightRange light_range = forward_plus_light_range(input.position, input.view_depth);
        [loop]
        for (uint i = 0; i < light_range.count; ++i) {
            ForwardPlusLightData light;
            if (!forward_plus_load_light(light_range, i, light)) {
                continue;
            }
            accumulate_local_light_pbr(
                Lo, input.world_pos, N, V, albedo_lin, F0, a, m,
                light.position_radius, light.color_intensity, light.params);
        }
    }

    float3 env_diffuse = eval_env_diffuse(N, albedo_lin, occlusion, m);
    float3 env_specular = eval_env_specular(N, V, F0, roughness, m, occlusion);
    float3 ambient_diffuse = albedo_lin * ambient.xyz * occlusion * (1.0 - m);
    float3 final_color = ambient_diffuse + env_diffuse + env_specular + Lo + sc_emissive.rgb * emissive_sample;

    if (fog_enabled != 0) {
        float fog_start = min(fog_range.x, fog_range.y);
        float fog_end = max(fog_range.x, fog_range.y);
        float fog_dist = max(scene_distance - fog_start, 0.0);
        float fog_span = max(fog_end - fog_start, 1e-4);
        float fog_norm = fog_dist / fog_span;
        float fog_density = lerp(2.5, 8.0, saturate(fog_amount));
        float transmittance = exp(-fog_density * pow(fog_norm, 1.35));
        final_color = final_color * transmittance + fog_color.rgb * (1.0 - transmittance);
    }

    final_color *= sc_exposure;
    final_color = tonemap_aces_fitted(final_color);
    final_color = pow(saturate(final_color), 1.0 / 2.2);
    if (scene_shadow_debug_mode != 0) {
        float3 debug_color = scene_shadow_cascade_debug_color(scene_shadow_cascade_index(scene_distance));
        final_color = lerp(final_color, debug_color, 0.65);
    }
    final_color = forward_plus_apply_debug(final_color, input.position, input.view_depth, 0.72);

    float out_alpha = sc_alpha_mode == 2u ? albedo_sample.a : 1.0;
    return float4(final_color, out_alpha);
}
