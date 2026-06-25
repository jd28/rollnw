#pragma once

#include "frame_storage_arena.hpp"
#include "render_context.hpp"

#include <nw/gfx/gfx.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nw::gfx {
struct CommandList;
} // namespace nw::gfx

namespace nw::render {

class ShaderProvider;

inline constexpr const char* kForwardPlusGpuTimerCull = "forward_plus_cull";

struct ForwardPlusConfig {
    uint32_t tile_size = 32;
    uint32_t depth_slices = 16;
    uint32_t max_lights_per_cluster = 128;
};

struct ForwardPlusRenderPolicy {
    bool enabled = true;
    bool auto_configure_area = true;
    // When true (and the compute pipeline is available), the per-cluster light gather runs
    // on the GPU instead of the CPU scatter passes. Falls back to the CPU path otherwise.
    bool gpu_culling = true;
    ForwardPlusConfig config{
        .tile_size = 64u,
        .depth_slices = 8u,
        .max_lights_per_cluster = 128u,
    };
    ForwardPlusDebugMode debug_mode = ForwardPlusDebugMode::off;
};

struct ForwardPlusRenderResult {
    bool prepared = false;
    bool gpu_culling = false;
    float prepare_seconds = 0.0f;
    float upload_seconds = 0.0f;
};

struct ForwardPlusLightClusterBounds {
    uint32_t min_x = 0;
    uint32_t max_x = 0;
    uint32_t min_y = 0;
    uint32_t max_y = 0;
    uint32_t min_z = 0;
    uint32_t max_z = 0;
    bool valid = false;
};

// Per-frame uniform for the GPU cull.
struct ForwardPlusCullParams {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 inverse_projection{1.0f};
    glm::vec4 viewport{0.0f};
    glm::vec4 depth_params{0.0f};
    glm::uvec4 dims{0u};
    // x = light_count, y = cluster_count, z = tile_size, w = reserved.
    glm::uvec4 counts{0u};
};

static_assert(sizeof(ForwardPlusCullParams) % 16 == 0);

struct ForwardPlusFrame {
    std::vector<ForwardPlusLightGpu> lights;
    std::vector<ForwardPlusClusterHeader> cluster_headers;
    std::vector<uint32_t> cluster_light_indices;
    std::vector<uint32_t> cluster_counts;
    std::vector<uint32_t> light_order;
    std::vector<ForwardPlusLightClusterBounds> light_cluster_bounds;
    ForwardPlusResources resources{};

    void clear();
};

class ForwardPlusRenderer {
public:
    explicit ForwardPlusRenderer(nw::gfx::Context* ctx) noexcept;
    ~ForwardPlusRenderer();

    [[nodiscard]] bool initialize(ShaderProvider& shader_provider) noexcept;

    // Builds the cluster/light buffers, uploads them, and publishes the result into
    // `ctx.forward_plus`. An empty `light_indices` (std::nullopt) culls every light in
    // `ctx.local_lights`; a span (even an empty one) restricts culling to that subset.
    ForwardPlusRenderResult prepare_render_context(
        ForwardPlusFrame& frame,
        nw::gfx::CommandList* cmd,
        RenderContext& ctx,
        int32_t viewport_x,
        int32_t viewport_y,
        uint32_t viewport_width,
        uint32_t viewport_height,
        const ForwardPlusRenderPolicy& policy,
        std::optional<std::span<const uint32_t>> light_indices = std::nullopt);

private:
    void upload_frame(ForwardPlusFrame& frame, nw::gfx::CommandList* cmd);
    // Runs the per-cluster gather on the GPU; returns false if the dispatch could not be
    // issued (caller then falls back to the CPU path). Fills frame.resources buffers.
    [[nodiscard]] bool dispatch_gpu_cull(
        ForwardPlusFrame& frame, nw::gfx::CommandList* cmd, const RenderContext& ctx, uint32_t cluster_count);
    [[nodiscard]] nw::gfx::StorageSpan upload_frame_storage(
        nw::gfx::CommandList* cmd, const void* data, uint32_t size, uint32_t alignment = 64);
    // Reserves an uninitialized frame-arena storage span (for buffers a compute pass writes).
    [[nodiscard]] nw::gfx::StorageSpan allocate_frame_storage(
        nw::gfx::CommandList* cmd, uint32_t size, uint32_t alignment = 64);

    nw::gfx::Context* ctx_ = nullptr;
    nw::gfx::Handle<nw::gfx::Pipeline> cull_pipeline_;
    std::array<FrameStorageArena, 2> upload_arenas_{};
};

[[nodiscard]] ForwardPlusConfig preview_forward_plus_config() noexcept;
[[nodiscard]] ForwardPlusConfig area_forward_plus_config(
    uint32_t visible_record_count,
    uint32_t visible_light_count) noexcept;

// Standalone CPU cull (no GPU upload), used by tests/tools. An empty `light_indices`
// (std::nullopt) culls every light in `ctx.local_lights`; a span restricts culling to
// that subset.
void prepare_forward_plus_frame(
    ForwardPlusFrame& frame,
    const RenderContext& ctx,
    int32_t viewport_x,
    int32_t viewport_y,
    uint32_t viewport_width,
    uint32_t viewport_height,
    std::optional<std::span<const uint32_t>> light_indices = std::nullopt,
    const ForwardPlusConfig& config = {});

} // namespace nw::render
