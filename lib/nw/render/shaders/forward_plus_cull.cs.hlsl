// Forward+ light culling compute shader.
// One thread per cluster gathers lights into a fixed-stride index list. The CPU
// uploads packed lights and camera/cluster constants; cluster membership is decided
// here so the GPU path does not depend on CPU-produced tile/slice bounds.

struct ForwardPlusLightData {
    float4 position_radius;
    float4 color_intensity;
    float4 params;
};

struct ClusterHeader {
    uint offset;
    uint count;
    uint _pad0;
    uint _pad1;
};

cbuffer CullParams : register(b1) {
    float4x4 g_view;
    float4x4 g_projection;
    float4x4 g_inverse_projection;
    float4 g_viewport;      // x, y, width, height
    float4 g_depth_params;  // near, far, log_far_over_near, orthographic_flag
    uint4 g_dims;           // x, y, z, max_lights_per_cluster
    uint4 g_counts;         // light_count, cluster_count, tile_size, reserved
};

StructuredBuffer<ForwardPlusLightData> g_lights : register(t2);      // storage slot 0 (read)
RWStructuredBuffer<ClusterHeader> g_cluster_headers : register(u3);  // storage slot 1 (write)
RWStructuredBuffer<uint> g_cluster_indices : register(u5);           // storage slot 2 (write)

float depth_for_slice_boundary(uint boundary)
{
    const uint dim_z = max(g_dims.z, 1u);
    const float near_plane = max(g_depth_params.x, 1.0e-4);
    const float far_plane = max(g_depth_params.y, near_plane + 1.0);
    const float t = clamp((float)boundary / (float)dim_z, 0.0, 1.0);
    if (g_depth_params.w >= 0.5) {
        return lerp(near_plane, far_plane, t);
    }
    return near_plane * exp(max(g_depth_params.z, 1.0e-4) * t);
}

float2 ndc_from_pixel(float2 pixel)
{
    const float2 viewport_size = max(g_viewport.zw, float2(1.0, 1.0));
    return pixel / viewport_size * 2.0 - 1.0;
}

float3 view_position_at_depth(float2 ndc, float depth)
{
    const float4 h = mul(g_inverse_projection, float4(ndc, 1.0, 1.0));
    const float inv_w = abs(h.w) > 1.0e-6 ? rcp(h.w) : 1.0;
    const float3 projected = h.xyz * inv_w;
    if (g_depth_params.w >= 0.5) {
        return float3(projected.xy, -depth);
    }
    const float scale = abs(projected.z) > 1.0e-6 ? (-depth / projected.z) : 0.0;
    return projected * scale;
}

void include_point_in_aabb(float3 sample_position, inout float3 bounds_min, inout float3 bounds_max)
{
    bounds_min = min(bounds_min, sample_position);
    bounds_max = max(bounds_max, sample_position);
}

void cluster_view_aabb(uint tx, uint ty, uint tz, out float3 bounds_min, out float3 bounds_max)
{
    const uint tile_size = max(g_counts.z, 1u);
    const float2 viewport_size = max(g_viewport.zw, float2(1.0, 1.0));
    const float2 pixel_min = float2((float)(tx * tile_size), (float)(ty * tile_size));
    const float2 pixel_max = min(float2((float)((tx + 1u) * tile_size), (float)((ty + 1u) * tile_size)), viewport_size);
    const float2 ndc_min = ndc_from_pixel(pixel_min);
    const float2 ndc_max = ndc_from_pixel(pixel_max);
    const float depth_near = depth_for_slice_boundary(tz);
    const float depth_far = min(depth_for_slice_boundary(tz + 1u), max(g_depth_params.y, g_depth_params.x + 1.0));

    bounds_min = float3(3.402823e+38, 3.402823e+38, 3.402823e+38);
    bounds_max = float3(-3.402823e+38, -3.402823e+38, -3.402823e+38);

    [unroll]
    for (uint z = 0u; z < 2u; ++z) {
        const float depth = z == 0u ? depth_near : depth_far;
        [unroll]
        for (uint y = 0u; y < 2u; ++y) {
            [unroll]
            for (uint x = 0u; x < 2u; ++x) {
                const float2 ndc = float2(x == 0u ? ndc_min.x : ndc_max.x, y == 0u ? ndc_min.y : ndc_max.y);
                include_point_in_aabb(view_position_at_depth(ndc, depth), bounds_min, bounds_max);
            }
        }
    }
}

bool sphere_intersects_aabb(float3 center, float radius, float3 bounds_min, float3 bounds_max)
{
    const float3 closest = clamp(center, bounds_min, bounds_max);
    const float3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

bool cluster_intersects_light(uint tx, uint ty, uint tz, ForwardPlusLightData light)
{
    const float radius = light.position_radius.w;
    const float intensity = light.color_intensity.w;
    if (radius <= 1.0e-4 || intensity <= 1.0e-4) {
        return false;
    }

    const float4 view_center = mul(g_view, float4(light.position_radius.xyz, 1.0));
    float3 bounds_min;
    float3 bounds_max;
    cluster_view_aabb(tx, ty, tz, bounds_min, bounds_max);
    return sphere_intersects_aabb(view_center.xyz, radius, bounds_min, bounds_max);
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint cluster = dispatch_thread_id.x;
    if (cluster >= g_counts.y) {
        return;
    }

    const uint dim_x = max(g_dims.x, 1u);
    const uint dim_y = max(g_dims.y, 1u);
    const uint max_lights = g_dims.w;

    const uint slice_size = dim_x * dim_y;
    const uint tz = cluster / slice_size;
    const uint within_slice = cluster - tz * slice_size;
    const uint ty = within_slice / dim_x;
    const uint tx = within_slice - ty * dim_x;

    const uint base = cluster * max_lights;
    uint count = 0u;
    for (uint i = 0u; i < g_counts.x && count < max_lights; ++i) {
        if (!cluster_intersects_light(tx, ty, tz, g_lights[i])) {
            continue;
        }
        g_cluster_indices[base + count] = i;
        ++count;
    }

    ClusterHeader header;
    header.offset = base;
    header.count = count;
    header._pad0 = 0u;
    header._pad1 = 0u;
    g_cluster_headers[cluster] = header;
}
