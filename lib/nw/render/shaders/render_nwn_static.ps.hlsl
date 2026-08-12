// NWN diffuse pixel shader for common RenderModel primitives.
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
    float4 sc_color_key_threshold;
    uint4  sc_plt_colors0;
    uint4  sc_plt_colors1;
    uint4  sc_plt_colors2;
};

struct PSInput {
    float4 position  : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 texcoord  : TEXCOORD2;
    float3 view_dir  : TEXCOORD3;
    float3 tangent   : TEXCOORD4;
    float3 bitangent : TEXCOORD5;
    float view_depth : TEXCOORD6;
};

float3 lambert(float3 normal, float3 light_direction, float3 color)
{
    return color * max(dot(normal, light_direction), 0.0);
}

float3 blinn_phong(
    float3 normal,
    float3 light_direction,
    float3 view_direction,
    float3 color,
    float roughness,
    float strength)
{
    const float3 half_direction = normalize(light_direction + view_direction);
    const float specular_power = (1.0 - roughness) * 48.0 + 4.0;
    const float specular = pow(max(dot(normal, half_direction), 0.0), specular_power);
    return color * specular * (1.0 - roughness) * strength;
}

float3 material_normal(float3 geom_normal, float3 tangent, float3 bitangent, float2 texcoord)
{
    float3 normal_sample = g_textures[NonUniformResourceIndex(sc_normal_index)].Sample(g_sampler, texcoord).xyz;
    normal_sample = normal_sample * 2.0 - 1.0;
    normal_sample.xy *= sc_normal_scale;
    normal_sample.z = sqrt(saturate(1.0 - dot(normal_sample.xy, normal_sample.xy)));
    return normalize(
        normal_sample.x * tangent
        + normal_sample.y * bitangent
        + normal_sample.z * geom_normal);
}

uint plt_selected_color(uint layer)
{
    switch (layer) {
    case 0: return sc_plt_colors0.x;
    case 1: return sc_plt_colors0.y;
    case 2: return sc_plt_colors0.z;
    case 3: return sc_plt_colors0.w;
    case 4: return sc_plt_colors1.x;
    case 5: return sc_plt_colors1.y;
    case 6: return sc_plt_colors1.z;
    case 7: return sc_plt_colors1.w;
    case 8: return sc_plt_colors2.x;
    case 9: return sc_plt_colors2.y;
    default: return 0;
    }
}

float4 unpack_rgba8(uint value)
{
    return float4(
        float(value & 0xffu),
        float((value >> 8u) & 0xffu),
        float((value >> 16u) & 0xffu),
        float((value >> 24u) & 0xffu)) / 255.0;
}

float3 srgb_to_linear(float3 color)
{
    const float3 low = color / 12.92;
    const float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(low, high, step(float3(0.04045, 0.04045, 0.04045), color));
}

float4 sample_plt(float2 texcoord)
{
    const uint texture_index = NonUniformResourceIndex(sc_albedo_index);
    uint width = 0;
    uint height = 0;
    g_textures[texture_index].GetDimensions(width, height);
    if (width == 0 || height == 0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    const uint2 pixel = min(uint2(texcoord * float2(width, height)), uint2(width - 1, height - 1));
    const float4 raw = g_textures[texture_index].Load(int3(pixel, 0));
    uint color = (uint)round(saturate(raw.x) * 255.0);
    const uint layer = (uint)round(saturate(raw.y) * 255.0);
    if (color == 255u || layer >= 10u) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    const uint palette_width = plt_palette_width(layer);
    const uint palette_height = plt_palette_height(layer);
    const uint row = plt_selected_color(layer);
    if (palette_width == 0u || palette_height == 0u || row >= palette_height) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    color = min(color, palette_width - 1u);
    float4 palette_color = unpack_rgba8(plt_palette_color(layer, row, color));
    palette_color.rgb = srgb_to_linear(palette_color.rgb);
    return palette_color;
}

void accumulate_local_light(
    inout float3 lighting,
    float3 world_position,
    float3 normal,
    float3 view_direction,
    float roughness,
    float specular_strength,
    float4 position_radius,
    float4 color_intensity,
    float4 light_params)
{
    const bool ambient_local_light = light_params.x >= 0.5;
    const float radius = max(position_radius.w, 1.0e-3);
    float3 light_delta = position_radius.xyz - world_position;
    if (ambient_local_light) {
        light_delta.z *= saturate(light_params.y);
    }

    const float distance_squared = dot(light_delta, light_delta);
    const float radius_squared = radius * radius;
    if (distance_squared >= radius_squared) {
        return;
    }

    const float3 light_direction = light_delta * rsqrt(max(distance_squared, 1.0e-5));
    const float falloff = saturate(distance_squared / radius_squared);
    float attenuation = saturate(1.0 - falloff);
    attenuation *= attenuation;
    float intensity = color_intensity.w * attenuation;
    const uint shadow_slot_plus_one = (uint)(light_params.z + 0.5);
    if (shadow_slot_plus_one > 0u) {
        intensity *= scene_local_shadow_visibility(
            shadow_slot_plus_one - 1u, world_position, normal, light_direction);
    }

    if (ambient_local_light) {
        lighting += color_intensity.xyz * intensity;
        return;
    }

    const float legacy_diffuse = saturate(dot(normal, light_direction) * 0.45 + 0.55);
    lighting += color_intensity.xyz * legacy_diffuse * intensity;
    lighting += blinn_phong(
        normal,
        light_direction,
        view_direction,
        color_intensity.xyz,
        roughness,
        specular_strength * 0.55) * intensity;
}

float4 main(PSInput input) : SV_Target
{
    const float3 view_direction = normalize(input.view_dir);
    float3 geom_normal = normalize(input.normal);
    float3 tangent = normalize(input.tangent);
    float3 bitangent = normalize(input.bitangent);
    if (sc_double_sided != 0u && dot(geom_normal, view_direction) < 0.0) {
        geom_normal = -geom_normal;
        tangent = -tangent;
        bitangent = -bitangent;
    }
    const float3 normal = material_normal(geom_normal, tangent, bitangent, input.texcoord);

    float4 texel = sc_plt_enabled != 0u
        ? sample_plt(input.texcoord)
        : g_textures[NonUniformResourceIndex(sc_albedo_index)].Sample(g_sampler, input.texcoord);
    if (sc_alpha_mode == 1u) {
        const float color_key_cutoff = sc_color_key_threshold.w;
        if (color_key_cutoff > 0.0) {
            const float3 delta = texel.rgb - sc_color_key_threshold.xyz;
            clip(dot(delta, delta) - color_key_cutoff * color_key_cutoff);
        }
        clip(texel.a - sc_alpha_cutoff);
        texel.a = 1.0;
    }

    const float4 albedo_sample = texel * sc_albedo;
    const float3 albedo = albedo_sample.rgb;
    const float4 surface_sample = g_textures[NonUniformResourceIndex(sc_surface_index)].Sample(
        g_sampler, input.texcoord);
    const float roughness = clamp(sc_roughness * surface_sample.g, 0.05, 1.0);
    const float scene_distance = length(input.view_dir);

    float3 lighting = float3(0.0, 0.0, 0.0);
    const float3 key_direction = normalize(-key_dir_intensity.xyz);
    const float key_shadow = scene_shadow_visibility(
        input.world_pos, normal, key_direction, scene_distance);
    lighting += lambert(normal, key_direction, key_color.xyz)
        * key_dir_intensity.w * key_shadow;
    lighting += blinn_phong(
        normal,
        key_direction,
        view_direction,
        key_color.xyz,
        roughness,
        sc_specular_strength) * key_dir_intensity.w * key_shadow;

    const float3 fill_direction = normalize(-fill_dir_intensity.xyz);
    lighting += lambert(normal, fill_direction, fill_color.xyz) * fill_dir_intensity.w;

    const float3 rim_direction = normalize(-rim_dir_intensity.xyz);
    const float rim_factor = pow(1.0 - max(dot(normal, view_direction), 0.0), 3.0);
    lighting += rim_color.xyz * rim_factor * rim_dir_intensity.w
        * max(dot(normal, rim_direction), 0.0);

    if (forward_plus_enabled()) {
        const ForwardPlusLightRange light_range = forward_plus_light_range(
            input.position, input.view_depth);
        [loop]
        for (uint i = 0; i < light_range.count; ++i) {
            ForwardPlusLightData light;
            if (!forward_plus_load_light(light_range, i, light)) {
                continue;
            }
            accumulate_local_light(
                lighting,
                input.world_pos,
                normal,
                view_direction,
                roughness,
                sc_specular_strength,
                light.position_radius,
                light.color_intensity,
                light.params);
        }
    }

    float3 final_color = albedo * (ambient.xyz + lighting);
    const float3 emissive_sample = g_textures[NonUniformResourceIndex(sc_emissive_index)].Sample(
        g_sampler, input.texcoord).rgb;
    final_color += emissive_sample * sc_emissive.rgb;

    float output_alpha = sc_alpha_mode == 1u ? 1.0 : albedo_sample.a;
    if (sc_alpha_mode == 2u) {
        final_color *= output_alpha;
    }

    if (fog_enabled != 0u) {
        const float fog_start = min(fog_range.x, fog_range.y);
        const float fog_end = max(fog_range.x, fog_range.y);
        const float fog_distance = max(scene_distance - fog_start, 0.0);
        const float fog_span = max(fog_end - fog_start, 1.0e-4);
        const float fog_normalized = fog_distance / fog_span;
        const float fog_density = lerp(2.5, 8.0, saturate(fog_amount));
        const float transmittance = exp(-fog_density * pow(fog_normalized, 1.35));
        const float fog_factor = 1.0 - saturate(transmittance);

        if (sc_alpha_mode == 2u && output_alpha < 0.999) {
            const float transparent_fog = saturate(
                fog_factor * (1.35 + (1.0 - output_alpha) * 1.5));
            const float visibility = 1.0 - transparent_fog;
            final_color *= visibility;
            output_alpha *= visibility;
        } else {
            final_color = final_color * transmittance + fog_color.rgb * (1.0 - transmittance);
        }
    }

    final_color *= sc_exposure;
    final_color = final_color / (final_color + float3(1.0, 1.0, 1.0));
    final_color = pow(saturate(final_color), 1.0 / 2.2);
    if (scene_shadow_debug_mode != 0u) {
        const float3 debug_color = scene_shadow_cascade_debug_color(
            scene_shadow_cascade_index(scene_distance));
        final_color = lerp(final_color, debug_color, 0.65);
    }
    final_color = forward_plus_apply_debug(final_color, input.position, input.view_depth, 0.72);
    return float4(final_color, output_alpha);
}
