// Shadow sampling from SceneConstants.
// Requires the bindless `g_textures` array and scene_constants.inc.hlsl.

uint scene_shadow_cascade_index(float scene_distance) {
    uint cascade = 0;
    if (scene_distance > scene_shadow_split_distances.x) cascade = 1;
    if (scene_distance > scene_shadow_split_distances.y) cascade = 2;
    return cascade;
}

float scene_shadow_cascade_blend_width(uint cascade) {
    if (cascade >= 2u) {
        return 0.0;
    }

    const float previous_split = cascade == 0u ? 0.0 : scene_shadow_split_distances[cascade - 1u];
    const float split = scene_shadow_split_distances[cascade];
    const float span = max(split - previous_split, 1.0);
    return min(8.0, span * 0.12);
}

float scene_shadow_cascade_blend_weight(uint cascade, float scene_distance) {
    if (cascade >= 2u || scene_shadow_texture_indices[cascade + 1u] == 0u) {
        return 0.0;
    }

    const float split = scene_shadow_split_distances[cascade];
    const float width = scene_shadow_cascade_blend_width(cascade);
    if (width <= 0.0) {
        return 0.0;
    }
    return saturate((scene_distance - (split - width)) / width);
}

float scene_shadow_sample_map(uint texture_index, float2 shadow_uv, float receiver_depth, float bias) {
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

float scene_shadow_sample_map_tent(
    uint texture_index,
    float2 shadow_uv,
    float receiver_depth,
    float bias,
    float filter_radius_texels)
{
    uint2 shadow_size = 0;
    g_textures[NonUniformResourceIndex(texture_index)].GetDimensions(shadow_size.x, shadow_size.y);
    if (shadow_size.x == 0 || shadow_size.y == 0) {
        return 1.0;
    }

    const float2 texel = 1.0 / float2(shadow_size);
    const float radius = max(filter_radius_texels, 0.5);
    float visibility = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
            float2 uv = saturate(shadow_uv + float2(x, y) * texel * radius);
            int2 pixel = min(int2(uv * float2(shadow_size)), int2(shadow_size) - 1);
            float shadow_depth = g_textures[NonUniformResourceIndex(texture_index)].Load(int3(pixel, 0)).x;
            visibility += weight * ((receiver_depth - bias) <= shadow_depth ? 1.0 : 0.0);
        }
    }
    return visibility * (1.0 / 16.0);
}

float scene_shadow_sample_cascade_filtered(uint cascade, float3 world_pos, float bias, float filter_radius_texels) {
    const uint shadow_texture_index = scene_shadow_texture_indices[cascade];
    if (shadow_texture_index == 0) {
        return 1.0;
    }

    float4 shadow_clip = mul(scene_shadow_world_to_shadow[cascade], float4(world_pos, 1.0));
    if (shadow_clip.w <= 0.0) {
        return 1.0;
    }

    float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    float2 shadow_uv = shadow_ndc.xy * 0.5 + 0.5;
    if (any(shadow_uv < 0.0.xx) || any(shadow_uv > 1.0.xx) || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0) {
        return 1.0;
    }

    return scene_shadow_sample_map_tent(shadow_texture_index, shadow_uv, shadow_ndc.z, bias, filter_radius_texels);
}

float scene_shadow_directional_bias(
    uint cascade,
    float ndotl,
    float bias_base,
    float bias_cascade_step,
    float bias_min,
    float bias_constant_step)
{
    return max(
        bias_min,
        (bias_base + bias_cascade_step * cascade) * (1.0 - ndotl) + bias_constant_step * cascade);
}

float3 scene_shadow_directional_receiver_pos(
    float3 world_pos,
    float3 N,
    float ndotl,
    uint cascade,
    float normal_offset_base,
    float normal_offset_cascade_step)
{
    const float offset = (normal_offset_base + normal_offset_cascade_step * cascade) * (1.0 - ndotl);
    return world_pos + N * offset;
}

float scene_shadow_directional_raw_visibility(
    float3 world_pos,
    float3 N,
    float3 L,
    float scene_distance,
    float bias_base,
    float bias_cascade_step,
    float bias_min,
    float bias_constant_step,
    float normal_offset_base,
    float normal_offset_cascade_step,
    float filter_radius_base,
    float filter_radius_cascade_step)
{
    const uint cascade = scene_shadow_cascade_index(scene_distance);
    const float ndotl = saturate(dot(N, L));
    const float3 offset_pos = scene_shadow_directional_receiver_pos(
        world_pos, N, ndotl, cascade, normal_offset_base, normal_offset_cascade_step);
    const float bias = scene_shadow_directional_bias(
        cascade, ndotl, bias_base, bias_cascade_step, bias_min, bias_constant_step);
    const float filter_radius = filter_radius_base + filter_radius_cascade_step * cascade;
    float visibility = scene_shadow_sample_cascade_filtered(cascade, offset_pos, bias, filter_radius);

    const float blend = scene_shadow_cascade_blend_weight(cascade, scene_distance);
    if (blend > 0.0) {
        const uint next_cascade = cascade + 1u;
        const float3 next_offset_pos = scene_shadow_directional_receiver_pos(
            world_pos, N, ndotl, next_cascade, normal_offset_base, normal_offset_cascade_step);
        const float next_bias = scene_shadow_directional_bias(
            next_cascade, ndotl, bias_base, bias_cascade_step, bias_min, bias_constant_step);
        const float next_filter_radius = filter_radius_base + filter_radius_cascade_step * next_cascade;
        const float next_visibility = scene_shadow_sample_cascade_filtered(
            next_cascade, next_offset_pos, next_bias, next_filter_radius);
        visibility = lerp(visibility, next_visibility, blend);
    }

    return visibility;
}

float scene_shadow_visibility(float3 world_pos, float3 N, float3 L, float scene_distance) {
    float visibility = scene_shadow_directional_raw_visibility(
        world_pos, N, L, scene_distance,
        0.0015, 0.0008, 0.00035, 0.0002,
        0.045, 0.030,
        1.15, 0.45);
    return lerp(1.0 - scene_shadow_strength, 1.0, visibility);
}

float3 scene_shadow_cascade_debug_color(uint cascade) {
    switch (cascade) {
    case 0: return float3(1.0, 0.25, 0.25);
    case 1: return float3(0.25, 1.0, 0.35);
    default: return float3(0.25, 0.55, 1.0);
    }
}

float scene_local_shadow_visibility(uint slot, float3 world_pos, float3 _N, float3 _L) {
    if (slot >= (uint)scene_local_shadow_params.y) {
        return 1.0;
    }
    const uint shadow_texture_index = scene_local_shadow_texture_indices[slot];
    if (shadow_texture_index == 0) {
        return 1.0;
    }

    float4 shadow_clip = mul(scene_local_shadow_world_to_shadow[slot], float4(world_pos, 1.0));
    if (shadow_clip.w <= 0.0) {
        return 1.0;
    }

    float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    float2 shadow_uv = shadow_ndc.xy * 0.5 + 0.5;
    if (any(shadow_uv < 0.0.xx) || any(shadow_uv > 1.0.xx) || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0) {
        return 1.0;
    }

    const float raw = scene_shadow_sample_map(shadow_texture_index, shadow_uv, shadow_ndc.z, 0.0);
    return lerp(1.0 - scene_local_shadow_params.x, 1.0, raw);
}
