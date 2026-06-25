#include "forward_plus_renderer.hpp"

#include "shader_provider.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace nw::render {
namespace {

uint32_t saturating_count(size_t value)
{
    return static_cast<uint32_t>(std::min<size_t>(value, std::numeric_limits<uint32_t>::max()));
}

uint32_t ceil_div(uint32_t value, uint32_t divisor)
{
    return divisor == 0 ? 0 : (value + divisor - 1u) / divisor;
}

using Clock = std::chrono::steady_clock;

float elapsed_seconds(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<float>(end - start).count();
}

bool finite_light(const nw::render::LocalLight& light) noexcept
{
    return std::isfinite(light.position.x) && std::isfinite(light.position.y) && std::isfinite(light.position.z)
        && std::isfinite(light.radius) && std::isfinite(light.color.x) && std::isfinite(light.color.y)
        && std::isfinite(light.color.z) && std::isfinite(light.intensity);
}

float finite_nonnegative(float value) noexcept
{
    return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
}

float light_sort_priority(const nw::render::LocalLight& light) noexcept
{
    const float color_max = std::max(
        finite_nonnegative(light.color.x),
        std::max(finite_nonnegative(light.color.y), finite_nonnegative(light.color.z)));
    const float radius = finite_nonnegative(light.radius);
    return finite_nonnegative(light.intensity) * color_max * radius * radius;
}

struct ForwardPlusDepthMapping {
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    float log_far_over_near = 1.0f;
    bool orthographic = false;
};

bool project_view_point(
    const glm::mat4& projection,
    const glm::vec4& view_point,
    uint32_t viewport_width,
    uint32_t viewport_height,
    float& out_x,
    float& out_y) noexcept
{
    const glm::vec4 clip = projection * view_point;
    if (!std::isfinite(clip.w) || clip.w <= 1.0e-4f) {
        return false;
    }

    const float inv_w = 1.0f / clip.w;
    const float ndc_x = clip.x * inv_w;
    const float ndc_y = clip.y * inv_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) {
        return false;
    }

    out_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(viewport_width);
    out_y = (ndc_y * 0.5f + 0.5f) * static_cast<float>(viewport_height);
    return std::isfinite(out_x) && std::isfinite(out_y);
}

bool projected_sphere_axis_slope_bounds(
    float axis_center,
    float center_depth,
    float radius,
    float& out_min_slope,
    float& out_max_slope) noexcept
{
    // Tangent slopes from the eye to the sphere in one projected axis/depth plane.
    const float radius2 = radius * radius;
    const float depth2 = center_depth * center_depth;
    const float denominator = depth2 - radius2;
    const float root2 = axis_center * axis_center + depth2 - radius2;
    if (denominator <= 1.0e-6f || root2 <= 0.0f) {
        return false;
    }

    const float root = radius * std::sqrt(root2);
    const float slope0 = (axis_center * center_depth - root) / denominator;
    const float slope1 = (axis_center * center_depth + root) / denominator;
    if (!std::isfinite(slope0) || !std::isfinite(slope1)) {
        return false;
    }

    out_min_slope = std::min(slope0, slope1);
    out_max_slope = std::max(slope0, slope1);
    return true;
}

bool project_perspective_sphere_bounds(
    const glm::mat4& projection,
    const glm::vec4& view_center,
    float radius,
    uint32_t viewport_width,
    uint32_t viewport_height,
    float& out_min_x,
    float& out_min_y,
    float& out_max_x,
    float& out_max_y) noexcept
{
    const float center_depth = -view_center.z;
    float min_slope_x = 0.0f;
    float max_slope_x = 0.0f;
    float min_slope_y = 0.0f;
    float max_slope_y = 0.0f;
    if (!projected_sphere_axis_slope_bounds(view_center.x, center_depth, radius, min_slope_x, max_slope_x)
        || !projected_sphere_axis_slope_bounds(view_center.y, center_depth, radius, min_slope_y, max_slope_y)) {
        return false;
    }

    const auto pixel_x = [viewport_width](float ndc_x) {
        return (ndc_x * 0.5f + 0.5f) * static_cast<float>(viewport_width);
    };
    const auto pixel_y = [viewport_height](float ndc_y) {
        return (ndc_y * 0.5f + 0.5f) * static_cast<float>(viewport_height);
    };
    const float ndc_x0 = projection[0][0] * min_slope_x - projection[2][0];
    const float ndc_x1 = projection[0][0] * max_slope_x - projection[2][0];
    const float ndc_y0 = projection[1][1] * min_slope_y - projection[2][1];
    const float ndc_y1 = projection[1][1] * max_slope_y - projection[2][1];

    const float pixel_x0 = pixel_x(ndc_x0);
    const float pixel_x1 = pixel_x(ndc_x1);
    const float pixel_y0 = pixel_y(ndc_y0);
    const float pixel_y1 = pixel_y(ndc_y1);
    if (!std::isfinite(pixel_x0) || !std::isfinite(pixel_x1)
        || !std::isfinite(pixel_y0) || !std::isfinite(pixel_y1)) {
        return false;
    }

    out_min_x = std::min(pixel_x0, pixel_x1);
    out_max_x = std::max(pixel_x0, pixel_x1);
    out_min_y = std::min(pixel_y0, pixel_y1);
    out_max_y = std::max(pixel_y0, pixel_y1);
    return true;
}

uint32_t depth_slice_for(
    float depth,
    const ForwardPlusDepthMapping& depth_mapping,
    uint32_t depth_slices) noexcept
{
    if (depth_slices <= 1u) {
        return 0;
    }

    float t = 0.0f;
    if (depth_mapping.orthographic) {
        t = (depth - depth_mapping.near_plane) / (depth_mapping.far_plane - depth_mapping.near_plane);
    } else {
        const float safe_depth = std::max(depth, depth_mapping.near_plane);
        t = std::log(safe_depth / depth_mapping.near_plane) / depth_mapping.log_far_over_near;
    }

    t = std::clamp(t, 0.0f, 0.999999f);
    return std::min(depth_slices - 1u, static_cast<uint32_t>(t * static_cast<float>(depth_slices)));
}

ForwardPlusLightClusterBounds cluster_bounds_for_light(
    const nw::render::LocalLight& light,
    const nw::render::RenderContext& ctx,
    const ForwardPlusDepthMapping& depth_mapping,
    const glm::uvec4& dims,
    uint32_t tile_size,
    uint32_t viewport_width,
    uint32_t viewport_height)
{
    ForwardPlusLightClusterBounds result{};
    if (!finite_light(light) || light.radius <= 1.0e-4f || light.intensity <= 1.0e-4f
        || dims.x == 0 || dims.y == 0 || dims.z == 0) {
        return result;
    }

    const glm::vec4 view_center = ctx.view * glm::vec4(light.position, 1.0f);
    const float center_depth = -view_center.z;
    const float depth_min = std::max(depth_mapping.near_plane, center_depth - light.radius);
    const float depth_max = std::min(depth_mapping.far_plane, center_depth + light.radius);
    if (depth_max <= depth_mapping.near_plane || depth_min >= depth_mapping.far_plane || depth_max < depth_min) {
        return result;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    bool full_screen = depth_min <= depth_mapping.near_plane * 1.01f;
    bool any_projected = false;
    if (!full_screen) {
        const float r = light.radius;
        if (!depth_mapping.orthographic
            && project_perspective_sphere_bounds(
                ctx.projection, view_center, r, viewport_width, viewport_height, min_x, min_y, max_x, max_y)) {
            any_projected = true;
        }

        if (!any_projected) {
            const std::array<glm::vec4, 8> points{
                view_center + glm::vec4{-r, -r, -r, 0.0f},
                view_center + glm::vec4{r, -r, -r, 0.0f},
                view_center + glm::vec4{-r, r, -r, 0.0f},
                view_center + glm::vec4{r, r, -r, 0.0f},
                view_center + glm::vec4{-r, -r, r, 0.0f},
                view_center + glm::vec4{r, -r, r, 0.0f},
                view_center + glm::vec4{-r, r, r, 0.0f},
                view_center + glm::vec4{r, r, r, 0.0f},
            };
            for (const auto& point : points) {
                float screen_x = 0.0f;
                float screen_y = 0.0f;
                if (!project_view_point(ctx.projection, point, viewport_width, viewport_height, screen_x, screen_y)) {
                    full_screen = true;
                    break;
                }
                min_x = std::min(min_x, screen_x);
                min_y = std::min(min_y, screen_y);
                max_x = std::max(max_x, screen_x);
                max_y = std::max(max_y, screen_y);
                any_projected = true;
            }
        }
    }

    if (full_screen) {
        min_x = 0.0f;
        min_y = 0.0f;
        max_x = static_cast<float>(viewport_width);
        max_y = static_cast<float>(viewport_height);
        any_projected = true;
    }
    if (!any_projected || max_x < 0.0f || max_y < 0.0f
        || min_x > static_cast<float>(viewport_width) || min_y > static_cast<float>(viewport_height)) {
        return result;
    }

    min_x = std::clamp(min_x, 0.0f, static_cast<float>(viewport_width));
    max_x = std::clamp(max_x, 0.0f, static_cast<float>(viewport_width));
    min_y = std::clamp(min_y, 0.0f, static_cast<float>(viewport_height));
    max_y = std::clamp(max_y, 0.0f, static_cast<float>(viewport_height));
    if (max_x < min_x || max_y < min_y) {
        return result;
    }

    const auto tile_for = [&](float pixel, uint32_t max_tile) {
        const float clamped = std::clamp(pixel, 0.0f, static_cast<float>(max_tile * tile_size + tile_size - 1u));
        return std::min(max_tile, static_cast<uint32_t>(clamped / static_cast<float>(tile_size)));
    };

    result.min_x = tile_for(std::floor(min_x), dims.x - 1u);
    result.max_x = tile_for(std::max(std::floor(max_x), min_x), dims.x - 1u);
    result.min_y = tile_for(std::floor(min_y), dims.y - 1u);
    result.max_y = tile_for(std::max(std::floor(max_y), min_y), dims.y - 1u);
    result.min_z = depth_slice_for(depth_min, depth_mapping, dims.z);
    result.max_z = depth_slice_for(depth_max, depth_mapping, dims.z);
    result.valid = result.min_x <= result.max_x && result.min_y <= result.max_y && result.min_z <= result.max_z;
    return result;
}

bool light_intersects_depth_range(
    const nw::render::LocalLight& light,
    const nw::render::RenderContext& ctx,
    const ForwardPlusDepthMapping& depth_mapping) noexcept
{
    if (!finite_light(light) || light.radius <= 1.0e-4f || light.intensity <= 1.0e-4f) {
        return false;
    }

    const glm::vec4 view_center = ctx.view * glm::vec4(light.position, 1.0f);
    const float center_depth = -view_center.z;
    const float depth_min = std::max(depth_mapping.near_plane, center_depth - light.radius);
    const float depth_max = std::min(depth_mapping.far_plane, center_depth + light.radius);
    return depth_max > depth_mapping.near_plane && depth_min < depth_mapping.far_plane && depth_max >= depth_min;
}

void count_light_range(
    const ForwardPlusLightClusterBounds& bounds,
    const glm::uvec4& dims,
    std::vector<uint32_t>& cluster_counts)
{
    if (!bounds.valid) {
        return;
    }
    for (uint32_t z = bounds.min_z; z <= bounds.max_z; ++z) {
        const uint32_t slice_offset = z * dims.y * dims.x;
        for (uint32_t y = bounds.min_y; y <= bounds.max_y; ++y) {
            const uint32_t row_begin = slice_offset + y * dims.x + bounds.min_x;
            const uint32_t row_end = slice_offset + y * dims.x + bounds.max_x;
            for (uint32_t cluster = row_begin; cluster <= row_end; ++cluster) {
                ++cluster_counts[cluster];
            }
        }
    }
}

void write_light_range(
    uint32_t light_index,
    const ForwardPlusLightClusterBounds& bounds,
    const glm::uvec4& dims,
    std::vector<uint32_t>& cluster_write_offsets,
    std::vector<uint32_t>& cluster_light_indices)
{
    if (!bounds.valid) {
        return;
    }
    for (uint32_t z = bounds.min_z; z <= bounds.max_z; ++z) {
        const uint32_t slice_offset = z * dims.y * dims.x;
        for (uint32_t y = bounds.min_y; y <= bounds.max_y; ++y) {
            const uint32_t row_begin = slice_offset + y * dims.x + bounds.min_x;
            const uint32_t row_end = slice_offset + y * dims.x + bounds.max_x;
            for (uint32_t cluster = row_begin; cluster <= row_end; ++cluster) {
                cluster_light_indices[cluster_write_offsets[cluster]++] = light_index;
            }
        }
    }
}

} // namespace

ForwardPlusConfig preview_forward_plus_config() noexcept
{
    return ForwardPlusConfig{
        .tile_size = 64u,
        .depth_slices = 8u,
    };
}

ForwardPlusConfig area_forward_plus_config(uint32_t visible_record_count, uint32_t visible_light_count) noexcept
{
    ForwardPlusConfig config{};
    if (visible_record_count <= 128u && visible_light_count <= 128u) {
        config.tile_size = 64u;
        config.depth_slices = 4u;
    } else if (visible_record_count <= 512u && visible_light_count <= 768u) {
        config.tile_size = 64u;
        config.depth_slices = 8u;
    }
    return config;
}

void ForwardPlusFrame::clear()
{
    lights.clear();
    cluster_headers.clear();
    cluster_light_indices.clear();
    cluster_counts.clear();
    light_order.clear();
    light_cluster_bounds.clear();
    resources = {};
}

ForwardPlusRenderer::ForwardPlusRenderer(nw::gfx::Context* ctx) noexcept
    : ctx_(ctx)
{
}

ForwardPlusRenderer::~ForwardPlusRenderer()
{
    if (cull_pipeline_.valid()) {
        nw::gfx::destroy_pipeline(ctx_, cull_pipeline_);
    }
    for (auto& arena : upload_arenas_) {
        arena.destroy();
    }
}

bool ForwardPlusRenderer::initialize(ShaderProvider& shader_provider) noexcept
{
    if (!ctx_) {
        return false;
    }
    // Optional: the GPU cull pipeline. If the shader is unavailable the renderer still
    // works via the CPU path; gpu_culling just stays a no-op.
    if (auto cs = shader_provider.get_shader("forward_plus_cull.cs.hlsl"); cs.valid()) {
        nw::gfx::ComputePipelineDesc desc{};
        desc.cs = cs;
        desc.uses_uniforms = true;
        desc.storage_buffer_count = 3;
        cull_pipeline_ = nw::gfx::create_compute_pipeline(ctx_, desc);
    }
    return true;
}

nw::gfx::StorageSpan ForwardPlusRenderer::upload_frame_storage(
    nw::gfx::CommandList* cmd, const void* data, uint32_t size, uint32_t alignment)
{
    if (!cmd || !data || size == 0) {
        return {};
    }

    nw::gfx::FrameInfo frame{};
    if (!nw::gfx::get_frame_info(ctx_, frame)) {
        return {};
    }

    const auto idx = std::min<size_t>(frame.frame_index, upload_arenas_.size() - 1);
    auto& arena = upload_arenas_[idx];
    if (arena.frame_id() != frame.frame_id) {
        if (!arena.reset(ctx_, frame.frame_id, size)) {
            return {};
        }
    }
    return arena.write(ctx_, data, size, alignment);
}

static void prepare_forward_plus_frame_impl(
    ForwardPlusFrame& frame,
    const nw::render::RenderContext& ctx,
    int32_t viewport_x,
    int32_t viewport_y,
    uint32_t viewport_width,
    uint32_t viewport_height,
    std::optional<std::span<const uint32_t>> light_indices,
    const ForwardPlusConfig& config,
    bool gpu_cull)
{
    frame.clear();
    const uint32_t tile_size = std::max(config.tile_size, 1u);
    const uint32_t depth_slices = std::max(config.depth_slices, 1u);
    const glm::uvec4 dims{
        ceil_div(viewport_width, tile_size),
        ceil_div(viewport_height, tile_size),
        depth_slices,
        0u,
    };
    const uint64_t cluster_count64 = static_cast<uint64_t>(dims.x) * dims.y * dims.z;
    if (viewport_width == 0 || viewport_height == 0 || cluster_count64 == 0
        || cluster_count64 > std::numeric_limits<uint32_t>::max()) {
        return;
    }

    const uint32_t cluster_count = static_cast<uint32_t>(cluster_count64);
    const uint32_t overflow_budget = std::max(config.max_lights_per_cluster, 1u);
    const float near_plane = std::max(ctx.camera_near_plane, 1.0e-4f);
    const float far_plane = std::max(ctx.camera_far_plane, near_plane + 1.0f);
    const ForwardPlusDepthMapping depth_mapping{
        .near_plane = near_plane,
        .far_plane = far_plane,
        .log_far_over_near = std::max(std::log(far_plane / near_plane), 1.0e-4f),
        .orthographic = ctx.orthographic_camera,
    };
    if (!gpu_cull) {
        frame.cluster_counts.resize(cluster_count, 0u);
        frame.cluster_headers.resize(cluster_count);
    }
    const size_t light_reserve = light_indices ? light_indices->size() : ctx.local_lights.size();
    frame.light_order.reserve(light_reserve);
    if (light_indices) {
        for (const uint32_t light_index : *light_indices) {
            if (light_index < ctx.local_lights.size()) {
                frame.light_order.push_back(light_index);
            }
        }
    } else {
        for (size_t light_index = 0; light_index < ctx.local_lights.size(); ++light_index) {
            frame.light_order.push_back(saturating_count(light_index));
        }
    }
    if (frame.light_order.size() > overflow_budget) {
        std::stable_sort(frame.light_order.begin(), frame.light_order.end(), [&](uint32_t lhs, uint32_t rhs) {
            return light_sort_priority(ctx.local_lights[lhs]) > light_sort_priority(ctx.local_lights[rhs]);
        });
    }
    frame.lights.reserve(light_reserve);
    if (!gpu_cull) {
        frame.light_cluster_bounds.reserve(light_reserve);
    }

    const auto light_phase_start = Clock::now();
    const auto append_light = [&](const nw::render::LocalLight& light) {
        if (gpu_cull) {
            if (!light_intersects_depth_range(light, ctx, depth_mapping)) {
                return;
            }
            frame.lights.push_back(nw::render::ForwardPlusLightGpu{
                .position_radius = glm::vec4(light.position, light.radius),
                .color_intensity = glm::vec4(light.color, light.intensity),
                .params = glm::vec4(
                    light.contribution == nw::render::LocalLightContribution::ambient ? 1.0f : 0.0f,
                    light.vertical_scale,
                    light.shadow_slot >= 0 ? static_cast<float>(light.shadow_slot + 1) : 0.0f,
                    0.0f),
            });
            return;
        }

        const auto bounds = cluster_bounds_for_light(
            light, ctx, depth_mapping, dims, tile_size, viewport_width, viewport_height);
        if (!bounds.valid) {
            return;
        }
        frame.lights.push_back(nw::render::ForwardPlusLightGpu{
            .position_radius = glm::vec4(light.position, light.radius),
            .color_intensity = glm::vec4(light.color, light.intensity),
            .params = glm::vec4(
                light.contribution == nw::render::LocalLightContribution::ambient ? 1.0f : 0.0f,
                light.vertical_scale,
                light.shadow_slot >= 0 ? static_cast<float>(light.shadow_slot + 1) : 0.0f,
                0.0f),
        });
        frame.light_cluster_bounds.push_back(bounds);
        count_light_range(bounds, dims, frame.cluster_counts);
    };

    for (const uint32_t light_index : frame.light_order) {
        append_light(ctx.local_lights[light_index]);
    }
    const auto light_phase_end = Clock::now();

    uint32_t offset = 0;
    uint32_t active_clusters = 0;
    uint32_t max_lights_per_cluster = 0;
    uint32_t overflow_clusters = 0;
    uint32_t overflow_lights = 0;
    auto header_phase_start = Clock::now();
    auto header_phase_end = header_phase_start;
    auto index_phase_start = header_phase_start;
    auto index_phase_end = header_phase_start;
    // CPU binning: prefix-sum the per-cluster counts into header offsets, then scatter light
    // indices. Skipped under gpu_cull, where the per-cluster gather runs on the GPU instead.
    if (!gpu_cull) {
        header_phase_start = Clock::now();
        for (uint32_t cluster = 0; cluster < cluster_count; ++cluster) {
            const uint32_t count = frame.cluster_counts[cluster];
            frame.cluster_headers[cluster].offset = offset;
            frame.cluster_headers[cluster].count = count;
            frame.cluster_counts[cluster] = offset;
            offset += count;
            if (count > 0) {
                ++active_clusters;
                max_lights_per_cluster = std::max(max_lights_per_cluster, count);
            }
            if (count > overflow_budget) {
                ++overflow_clusters;
                overflow_lights += count - overflow_budget;
            }
        }
        header_phase_end = Clock::now();

        index_phase_start = Clock::now();
        frame.cluster_light_indices.resize(offset);
        auto& cluster_write_offsets = frame.cluster_counts;
        for (uint32_t light_index = 0; light_index < frame.light_cluster_bounds.size(); ++light_index) {
            write_light_range(light_index, frame.light_cluster_bounds[light_index], dims,
                cluster_write_offsets, frame.cluster_light_indices);
        }
        index_phase_end = Clock::now();
    }

    auto& resources = frame.resources;
    resources.enabled = !frame.lights.empty();
    resources.tile_size = tile_size;
    resources.max_lights_per_cluster = overflow_budget;
    resources.cluster_dimensions = dims;
    resources.viewport = glm::vec4(
        static_cast<float>(viewport_x),
        static_cast<float>(viewport_y),
        static_cast<float>(viewport_width),
        static_cast<float>(viewport_height));
    resources.depth_params = glm::vec4(
        near_plane,
        far_plane,
        depth_mapping.orthographic ? 0.0f : depth_mapping.log_far_over_near,
        depth_mapping.orthographic ? 1.0f : 0.0f);
    resources.stats.light_cull_seconds = elapsed_seconds(light_phase_start, light_phase_end);
    resources.stats.cluster_header_seconds = elapsed_seconds(header_phase_start, header_phase_end);
    resources.stats.cluster_index_seconds = elapsed_seconds(index_phase_start, index_phase_end);
    resources.stats.light_count = saturating_count(frame.lights.size());
    resources.stats.cluster_count = cluster_count;
    resources.stats.active_cluster_count = active_clusters;
    resources.stats.cluster_light_index_count = saturating_count(frame.cluster_light_indices.size());
    resources.stats.cluster_light_index_capacity = resources.stats.cluster_light_index_count;
    resources.stats.max_lights_per_cluster = max_lights_per_cluster;
    resources.stats.overflow_cluster_count = overflow_clusters;
    resources.stats.overflow_light_count = overflow_lights;
    resources.stats.upload_bytes = saturating_count(
        frame.lights.size() * sizeof(nw::render::ForwardPlusLightGpu)
        + frame.cluster_headers.size() * sizeof(nw::render::ForwardPlusClusterHeader)
        + frame.cluster_light_indices.size() * sizeof(uint32_t));
}

void prepare_forward_plus_frame(
    ForwardPlusFrame& frame,
    const nw::render::RenderContext& ctx,
    int32_t viewport_x,
    int32_t viewport_y,
    uint32_t viewport_width,
    uint32_t viewport_height,
    std::optional<std::span<const uint32_t>> light_indices,
    const ForwardPlusConfig& config)
{
    prepare_forward_plus_frame_impl(
        frame, ctx, viewport_x, viewport_y, viewport_width, viewport_height, light_indices, config, false);
}

nw::gfx::StorageSpan ForwardPlusRenderer::allocate_frame_storage(
    nw::gfx::CommandList* cmd, uint32_t size, uint32_t alignment)
{
    if (!cmd || size == 0) {
        return {};
    }
    nw::gfx::FrameInfo frame{};
    if (!nw::gfx::get_frame_info(ctx_, frame)) {
        return {};
    }
    const auto idx = std::min<size_t>(frame.frame_index, upload_arenas_.size() - 1);
    auto& arena = upload_arenas_[idx];
    if (arena.frame_id() != frame.frame_id) {
        if (!arena.reset(ctx_, frame.frame_id, size)) {
            return {};
        }
    }
    return arena.allocate_mapped(ctx_, size, alignment).span;
}

bool ForwardPlusRenderer::dispatch_gpu_cull(
    ForwardPlusFrame& frame, nw::gfx::CommandList* cmd, const RenderContext& ctx, uint32_t cluster_count)
{
    auto& resources = frame.resources;
    if (!cmd || !cull_pipeline_.valid() || cluster_count == 0 || frame.lights.empty()) {
        return false;
    }
    const uint32_t max_lights = std::max(resources.max_lights_per_cluster, 1u);

    // The same packed lights are read by the compute cull and later by the pixel shader.
    const auto light_span = upload_frame_storage(
        cmd, frame.lights.data(), saturating_count(frame.lights.size() * sizeof(frame.lights.front())));
    if (!light_span.buffer.valid()) {
        return false;
    }

    // Outputs written by the compute pass: per-cluster headers and the fixed-stride index list.
    const uint64_t index_count = static_cast<uint64_t>(cluster_count) * max_lights;
    const uint32_t header_bytes = saturating_count(cluster_count * sizeof(nw::render::ForwardPlusClusterHeader));
    const uint32_t index_bytes = saturating_count(index_count * sizeof(uint32_t));
    const auto header_span = allocate_frame_storage(cmd, header_bytes);
    const auto index_span = allocate_frame_storage(cmd, index_bytes);
    if (!header_span.buffer.valid() || !index_span.buffer.valid()) {
        return false;
    }

    ForwardPlusCullParams params{};
    params.view = ctx.view;
    params.projection = ctx.projection;
    params.inverse_projection = glm::inverse(ctx.projection);
    params.dims = glm::uvec4(
        resources.cluster_dimensions.x, resources.cluster_dimensions.y, resources.cluster_dimensions.z, max_lights);
    params.counts = glm::uvec4(saturating_count(frame.lights.size()), cluster_count, resources.tile_size, 0u);
    params.viewport = resources.viewport;
    params.depth_params = resources.depth_params;
    auto uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(params));
    if (!uniforms.data) {
        return false;
    }
    std::memcpy(uniforms.data, &params, sizeof(params));

    const nw::gfx::GpuTimerScope timer_scope = nw::gfx::cmd_begin_gpu_timer(cmd, kForwardPlusGpuTimerCull);
    nw::gfx::cmd_bind_pipeline(cmd, cull_pipeline_);
    nw::gfx::cmd_bind_compute_resources(cmd, cull_pipeline_, uniforms, light_span, header_span, index_span);
    nw::gfx::cmd_dispatch(cmd, (cluster_count + 63u) / 64u, 1, 1);
    if (timer_scope.valid()) {
        nw::gfx::cmd_end_gpu_timer(cmd, timer_scope);
    }
    nw::gfx::cmd_barrier_compute_to_graphics(cmd);

    resources.light_buffer = light_span;
    resources.cluster_header_buffer = header_span;
    resources.cluster_light_index_buffer = index_span;
    resources.enabled = true;
    resources.stats.cluster_light_index_count = 0;
    resources.stats.cluster_light_index_capacity = saturating_count(index_count);
    resources.stats.upload_bytes = saturating_count(
        frame.lights.size() * sizeof(nw::render::ForwardPlusLightGpu)
        + header_bytes + index_bytes);
    return true;
}

void ForwardPlusRenderer::upload_frame(ForwardPlusFrame& frame, nw::gfx::CommandList* cmd)
{
    auto& resources = frame.resources;
    if (!resources.enabled || !cmd) {
        resources.light_buffer = {};
        resources.cluster_header_buffer = {};
        resources.cluster_light_index_buffer = {};
        return;
    }

    if (!frame.lights.empty()) {
        resources.light_buffer = upload_frame_storage(
            cmd, frame.lights.data(), saturating_count(frame.lights.size() * sizeof(frame.lights.front())));
    }
    if (!frame.cluster_headers.empty()) {
        resources.cluster_header_buffer = upload_frame_storage(
            cmd,
            frame.cluster_headers.data(),
            saturating_count(frame.cluster_headers.size() * sizeof(frame.cluster_headers.front())));
    }
    if (!frame.cluster_light_indices.empty()) {
        resources.cluster_light_index_buffer = upload_frame_storage(
            cmd,
            frame.cluster_light_indices.data(),
            saturating_count(frame.cluster_light_indices.size() * sizeof(frame.cluster_light_indices.front())));
    }
    resources.enabled = resources.light_buffer.buffer.valid()
        && resources.cluster_header_buffer.buffer.valid()
        && (frame.cluster_light_indices.empty() || resources.cluster_light_index_buffer.buffer.valid());
}

ForwardPlusRenderResult ForwardPlusRenderer::prepare_render_context(
    ForwardPlusFrame& frame,
    nw::gfx::CommandList* cmd,
    RenderContext& ctx,
    int32_t viewport_x,
    int32_t viewport_y,
    uint32_t viewport_width,
    uint32_t viewport_height,
    const ForwardPlusRenderPolicy& policy,
    std::optional<std::span<const uint32_t>> light_indices)
{
    ctx.forward_plus_debug_mode = policy.debug_mode;
    if (!policy.enabled || ctx.local_lights.empty()) {
        frame.clear();
        ctx.forward_plus = {};
        ctx.local_lights = {};
        return {};
    }

    const uint32_t overflow_budget = std::max(policy.config.max_lights_per_cluster, 1u);
    const size_t candidate_light_count = light_indices ? light_indices->size() : ctx.local_lights.size();
    const bool overflow_risk = candidate_light_count > overflow_budget;
    // GPU cull writes fixed-stride capped lists and has no same-frame overflow readback.
    // Use CPU binning for overflow-risk frames so stats report truncation explicitly.
    const bool gpu_cull = policy.gpu_culling && cull_pipeline_.valid() && !overflow_risk;
    const auto prepare_start = Clock::now();
    prepare_forward_plus_frame_impl(
        frame, ctx, viewport_x, viewport_y, viewport_width, viewport_height, light_indices, policy.config, gpu_cull);
    const auto prepare_end = Clock::now();
    bool culled = false;
    if (gpu_cull) {
        const glm::uvec4 dims = frame.resources.cluster_dimensions;
        culled = dispatch_gpu_cull(frame, cmd, ctx, dims.x * dims.y * dims.z);
        if (!culled) {
            // GPU dispatch could not be issued; re-run the CPU binning and upload instead.
            prepare_forward_plus_frame_impl(frame, ctx, viewport_x, viewport_y, viewport_width,
                viewport_height, light_indices, policy.config, false);
        }
    }
    if (!culled) {
        upload_frame(frame, cmd);
    }
    const auto upload_end = Clock::now();

    ctx.forward_plus = frame.resources;
    ctx.local_lights = {};
    return ForwardPlusRenderResult{
        .prepared = true,
        .gpu_culling = culled,
        .prepare_seconds = elapsed_seconds(prepare_start, prepare_end),
        .upload_seconds = elapsed_seconds(prepare_end, upload_end),
    };
}

} // namespace nw::render
