// Shared cascaded shadow-map sampling.
// Requires the bindless `g_textures` array to be declared before inclusion.
// Layout of ShadowConstants must match the C++ side exactly.

cbuffer ShadowConstants : register(b4) {
    float4x4 world_to_shadow[3];
    uint4 shadow_texture_indices;
    float4 shadow_split_distances;
    float shadow_strength;
    uint debug_mode;
    float2 _shadow_pad;
    // Local-light shadows: slot i maps to ForwardPlusLightData.params.z == i+1.
    float4x4 local_world_to_shadow[4];
    uint4 local_shadow_texture_indices;
    // x = strength, y = active count, zw = pad.
    float4 local_shadow_params;
};

uint shadow_cascade_index(float scene_distance) {
    uint cascade = 0;
    if (scene_distance > shadow_split_distances.x) cascade = 1;
    if (scene_distance > shadow_split_distances.y) cascade = 2;
    return cascade;
}

float shadow_cascade_blend_width(uint cascade) {
    if (cascade >= 2u) {
        return 0.0;
    }

    const float previous_split = cascade == 0u ? 0.0 : shadow_split_distances[cascade - 1u];
    const float split = shadow_split_distances[cascade];
    const float span = max(split - previous_split, 1.0);
    return min(8.0, span * 0.12);
}

float shadow_cascade_blend_weight(uint cascade, float scene_distance) {
    if (cascade >= 2u || shadow_texture_indices[cascade + 1u] == 0u) {
        return 0.0;
    }

    const float split = shadow_split_distances[cascade];
    const float width = shadow_cascade_blend_width(cascade);
    if (width <= 0.0) {
        return 0.0;
    }
    return saturate((scene_distance - (split - width)) / width);
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

float sample_shadow_map_tent(
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

// Raw cascade visibility in [0, 1]; returns 1.0 (fully lit) whenever the
// cascade has no shadow map or the position falls outside its frustum.
float shadow_sample_cascade(uint cascade, float3 world_pos, float bias) {
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

    return sample_shadow_map(shadow_texture_index, shadow_uv, shadow_ndc.z, bias);
}

float shadow_sample_cascade_filtered(uint cascade, float3 world_pos, float bias, float filter_radius_texels) {
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

    return sample_shadow_map_tent(shadow_texture_index, shadow_uv, shadow_ndc.z, bias, filter_radius_texels);
}

float shadow_directional_bias(
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

float3 shadow_directional_receiver_pos(
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

float shadow_directional_raw_visibility(
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
    const uint cascade = shadow_cascade_index(scene_distance);
    const float ndotl = saturate(dot(N, L));
    const float3 offset_pos = shadow_directional_receiver_pos(
        world_pos, N, ndotl, cascade, normal_offset_base, normal_offset_cascade_step);
    const float bias = shadow_directional_bias(
        cascade, ndotl, bias_base, bias_cascade_step, bias_min, bias_constant_step);
    const float filter_radius = filter_radius_base + filter_radius_cascade_step * cascade;
    float visibility = shadow_sample_cascade_filtered(cascade, offset_pos, bias, filter_radius);

    const float blend = shadow_cascade_blend_weight(cascade, scene_distance);
    if (blend > 0.0) {
        const uint next_cascade = cascade + 1u;
        const float3 next_offset_pos = shadow_directional_receiver_pos(
            world_pos, N, ndotl, next_cascade, normal_offset_base, normal_offset_cascade_step);
        const float next_bias = shadow_directional_bias(
            next_cascade, ndotl, bias_base, bias_cascade_step, bias_min, bias_constant_step);
        const float next_filter_radius = filter_radius_base + filter_radius_cascade_step * next_cascade;
        const float next_visibility = shadow_sample_cascade_filtered(
            next_cascade, next_offset_pos, next_bias, next_filter_radius);
        visibility = lerp(visibility, next_visibility, blend);
    }

    return visibility;
}

// Visibility in [1-strength, 1] for a single local-light shadow slot (0-based).
// Returns 1.0 (fully lit) outside the caster's local-shadow frustum so unshadowed
// regions fall back to the light's full contribution. The caller weights only the
// casting light's own term by this value, never the whole surface.
//
// Local maps are shallow top-down ortho maps. Sample the receiver at its unoffset
// position: the old normal/slope receiver bias erased real occlusion in NWN
// interiors where floors and props are close in local-shadow depth.
float local_shadow_visibility(uint slot, float3 world_pos, float3 _N, float3 _L) {
    if (slot >= (uint)local_shadow_params.y) {
        return 1.0;
    }
    const uint shadow_texture_index = local_shadow_texture_indices[slot];
    if (shadow_texture_index == 0) {
        return 1.0;
    }

    const float3 offset_pos = world_pos;
    float4 shadow_clip = mul(local_world_to_shadow[slot], float4(offset_pos, 1.0));
    if (shadow_clip.w <= 0.0) {
        return 1.0;
    }

    float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    float2 shadow_uv = shadow_ndc.xy * 0.5 + 0.5;
    if (any(shadow_uv < 0.0.xx) || any(shadow_uv > 1.0.xx) || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0) {
        return 1.0;
    }

    const float bias = 0.0;
    const float raw = sample_shadow_map(shadow_texture_index, shadow_uv, shadow_ndc.z, bias);
    return lerp(1.0 - local_shadow_params.x, 1.0, raw);
}
