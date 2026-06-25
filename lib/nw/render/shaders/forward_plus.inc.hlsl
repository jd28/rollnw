struct ForwardPlusClusterHeader {
    uint offset;
    uint count;
    uint _pad0;
    uint _pad1;
};

struct ForwardPlusLightData {
    float4 position_radius;
    float4 color_intensity;
    float4 params;
};

struct ForwardPlusLightRange {
    uint offset;
    uint count;
    uint total_light_count;
    uint _pad0;
};

StructuredBuffer<ForwardPlusClusterHeader> g_forward_plus_clusters : register(t5, space0);
StructuredBuffer<uint> g_forward_plus_light_indices : register(t6, space0);
StructuredBuffer<ForwardPlusLightData> g_forward_plus_lights : register(t7, space0);

bool forward_plus_enabled() {
    return forward_plus_params.x != 0
        && forward_plus_cluster_dims.x != 0
        && forward_plus_cluster_dims.y != 0
        && forward_plus_cluster_dims.z != 0;
}

uint forward_plus_debug_mode() {
    return forward_plus_params.w;
}

uint forward_plus_depth_slice(float view_depth) {
    const uint dim_z = max(forward_plus_cluster_dims.z, 1u);
    const float near_plane = max(forward_plus_depth_params.x, 1.0e-4);
    const float far_plane = max(forward_plus_depth_params.y, near_plane + 1.0);
    const float depth = max(view_depth, near_plane);
    float t = 0.0;
    if (forward_plus_depth_params.w >= 0.5) {
        t = (depth - near_plane) / (far_plane - near_plane);
    } else {
        t = log(depth / near_plane) / max(forward_plus_depth_params.z, 1.0e-4);
    }
    t = clamp(t, 0.0, 0.999999);
    return min(dim_z - 1u, (uint)(t * (float)dim_z));
}

uint forward_plus_cluster_index(float4 screen_position, float view_depth) {
    const uint dim_x = max(forward_plus_cluster_dims.x, 1u);
    const uint dim_y = max(forward_plus_cluster_dims.y, 1u);
    const uint tile_size = max(forward_plus_cluster_dims.w, 1u);

    const float2 pixel = max(screen_position.xy - forward_plus_viewport.xy, float2(0.0, 0.0));
    const uint tile_x = min(dim_x - 1u, (uint)(pixel.x / (float)tile_size));
    const uint tile_y = min(dim_y - 1u, (uint)(pixel.y / (float)tile_size));

    const uint tile_z = forward_plus_depth_slice(view_depth);
    return (tile_z * dim_y + tile_y) * dim_x + tile_x;
}

ForwardPlusLightRange forward_plus_light_range(float4 screen_position, float view_depth) {
    const uint cluster_index = forward_plus_cluster_index(screen_position, view_depth);
    const ForwardPlusClusterHeader cluster = g_forward_plus_clusters[cluster_index];

    ForwardPlusLightRange range;
    range.offset = cluster.offset;
    range.count = min(cluster.count, forward_plus_params.y);
    range.total_light_count = forward_plus_params.z;
    range._pad0 = 0u;
    return range;
}

void forward_plus_clear_light(out ForwardPlusLightData light) {
    light.position_radius = float4(0.0, 0.0, 0.0, 0.0);
    light.color_intensity = float4(0.0, 0.0, 0.0, 0.0);
    light.params = float4(0.0, 0.0, 0.0, 0.0);
}

bool forward_plus_load_light(ForwardPlusLightRange range, uint ordinal, out ForwardPlusLightData light) {
    if (ordinal >= range.count) {
        forward_plus_clear_light(light);
        return false;
    }

    const uint light_index = g_forward_plus_light_indices[range.offset + ordinal];
    if (light_index >= range.total_light_count) {
        forward_plus_clear_light(light);
        return false;
    }

    light = g_forward_plus_lights[light_index];
    return true;
}

float3 forward_plus_heat_color(float value) {
    const float t = saturate(value);
    const float3 cold = float3(0.035, 0.12, 0.42);
    const float3 mid = float3(0.0, 0.72, 0.36);
    const float3 hot = float3(1.0, 0.82, 0.12);
    const float3 maxed = float3(1.0, 0.08, 0.18);
    float3 color = lerp(cold, mid, smoothstep(0.0, 0.45, t));
    color = lerp(color, hot, smoothstep(0.38, 0.78, t));
    color = lerp(color, maxed, smoothstep(0.72, 1.0, t));
    return color;
}

float forward_plus_tile_grid(float4 screen_position) {
    const uint tile_size = max(forward_plus_cluster_dims.w, 1u);
    const float2 pixel = max(screen_position.xy - forward_plus_viewport.xy, float2(0.0, 0.0));
    const float2 cell = frac(pixel / (float)tile_size);
    const float2 near_min = 1.0 - step(float2(0.035, 0.035), cell);
    const float2 near_max = step(float2(0.965, 0.965), cell);
    return saturate(max(max(near_min.x, near_min.y), max(near_max.x, near_max.y)));
}

float3 forward_plus_debug_color(float4 screen_position, float view_depth) {
    const uint mode = forward_plus_debug_mode();
    if (mode == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    if (!forward_plus_enabled()) {
        return float3(0.30, 0.03, 0.06);
    }

    float3 debug_color = float3(0.0, 0.0, 0.0);
    if (mode == 2u) {
        const uint depth_slice = forward_plus_depth_slice(view_depth);
        const float depth_slice_count = max((float)forward_plus_cluster_dims.z, 1.0);
        const float t = ((float)depth_slice + 0.5) / depth_slice_count;
        debug_color = 0.52 + 0.48 * cos(6.2831853 * (t + float3(0.00, 0.33, 0.67)));
    } else {
        const ForwardPlusLightRange range = forward_plus_light_range(screen_position, view_depth);
        const float max_lights = max((float)forward_plus_params.y, 1.0);
        const float light_ratio = saturate((float)range.count / max_lights);
        debug_color = range.count == 0u ? float3(0.018, 0.025, 0.055) : forward_plus_heat_color(light_ratio);
        if (range.count >= forward_plus_params.y && forward_plus_params.y > 0u) {
            debug_color = float3(1.0, 0.0, 0.82);
        }
    }

    const float grid = forward_plus_tile_grid(screen_position);
    return lerp(debug_color, float3(1.0, 1.0, 1.0), grid * 0.42);
}

float3 forward_plus_apply_debug(float3 color, float4 screen_position, float view_depth, float opacity) {
    if (forward_plus_debug_mode() == 0u) {
        return color;
    }
    return lerp(color, forward_plus_debug_color(screen_position, view_depth), saturate(opacity));
}
