#pragma once

#include "../../gfx.hpp"

#include <array>
#include <cstdint>
#include <vector>

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL3/SDL_metal.h>
#include <cstdlib>
#include <mutex>
#include <string>

#define GFX_CHECK(cond, ...)                                         \
    do {                                                             \
        if (!(cond)) {                                               \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,               \
                "GFX_CHECK failed (%s:%d): ", __FILE__, __LINE__);   \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); \
            std::abort();                                            \
        }                                                            \
    } while (0)
#endif

namespace nw::gfx {

#ifdef __OBJC__

struct MetalCore;
struct MetalContext;

inline Context* as_gfx(MetalContext* value) noexcept { return reinterpret_cast<Context*>(value); }
inline MetalContext* as_metal(Context* value) noexcept { return reinterpret_cast<MetalContext*>(value); }
inline Core* as_gfx(MetalCore* core) noexcept { return reinterpret_cast<Core*>(core); }
inline MetalCore* as_metal(Core* core) noexcept { return reinterpret_cast<MetalCore*>(core); }

constexpr uint32_t kMetalFramesInFlight = kFramesInFlight;
constexpr uint32_t kMetalMaxGpuTimersPerFrame = 64;
constexpr uint32_t kMetalMaxGpuTimestampSamplesPerFrame = kMetalMaxGpuTimersPerFrame * 2;
constexpr uint32_t kMetalBindlessTextureCapacity = 4096;
constexpr uint32_t kMetalBindlessBufferIndex = 8;
constexpr uint32_t kMetalMinBufferOffsetAlignment = 256;

inline MTLPixelFormat to_mtl_format(Fmt fmt) noexcept
{
    switch (fmt) {
    case Fmt::RGBA8:
        return MTLPixelFormatRGBA8Unorm;
    case Fmt::RGBA8Srgb:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case Fmt::RGBA16F:
        return MTLPixelFormatRGBA16Float;
    case Fmt::D32FS8:
        return MTLPixelFormatDepth32Float_Stencil8;
    case Fmt::D32F:
        return MTLPixelFormatDepth32Float;
    default:
        return MTLPixelFormatInvalid;
    }
}

inline bool is_mtl_depth_format(MTLPixelFormat fmt) noexcept
{
    return fmt == MTLPixelFormatDepth32Float || fmt == MTLPixelFormatDepth32Float_Stencil8;
}

inline bool has_mtl_stencil_component(MTLPixelFormat fmt) noexcept
{
    return fmt == MTLPixelFormatDepth32Float_Stencil8;
}

struct MetalCore {
    MetalContext* context = nullptr;
    bool validation_enabled = false;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> command_queue = nil;
    id<MTLCounterSet> timestamp_counter_set = nil;
    bool counter_sampling_at_stage_boundary = false;
    bool counter_sampling_at_draw_boundary = false;
    uint64_t live_buffer_allocation_count = 0;
    uint64_t live_buffer_allocation_bytes = 0;

    std::mutex validation_report_mutex;
    ValidationReport validation_report{};
};

void record_metal_validation_error(MetalCore* core, const char* message) noexcept;
void record_metal_validation_warning(MetalCore* core, const char* message) noexcept;

struct MetalBuffer {
    MetalCore* core = nullptr;
    id<MTLBuffer> buffer = nil;
    void* mapped = nullptr;
    uint64_t gpu_address = 0;
    size_t size = 0;
    bool cpu_visible = false;
};

struct MetalTexture {
    id<MTLTexture> texture = nil;
    MTLPixelFormat format = MTLPixelFormatInvalid;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    uint32_t mip_levels = 1;
    uint32_t bindless_slot = 0;
    uint32_t resident_index = UINT32_MAX;
    bool owned = true;
    bool render_target = false;
    bool uploaded = false;
};

struct MetalRenderTarget {
    RenderTargetDesc desc{};
};

struct MetalShader {
    MetalCore* core = nullptr;
    id<MTLLibrary> library = nil;
    id<MTLFunction> function = nil;
    uint32_t threadgroup_size[3] = {1, 1, 1};
};

struct MetalPipeline {
    MetalCore* core = nullptr;
    id<MTLRenderPipelineState> render_state = nil;
    id<MTLDepthStencilState> depth_stencil_state = nil;
    id<MTLComputePipelineState> compute_state = nil;
    MTLCullMode cull_mode = MTLCullModeNone;
    uint8_t stencil_ref = 0;
    bool stencil_test = false;
    bool is_compute = false;
    uint32_t threadgroup_size[3] = {1, 1, 1};

    bool uses_draw_uniforms = true;
    bool uses_draw_uniforms2 = false;
    bool uses_single_texture = true;
    bool uses_bindless_sampled_textures = false;
    bool uses_storage_buffer = false;
    uint8_t storage_buffer_count = 0;
};

extern Pool<Buffer, MetalBuffer> g_metal_buffer_pool;
extern Pool<Shader, MetalShader> g_metal_shader_pool;
extern Pool<Pipeline, MetalPipeline> g_metal_pipeline_pool;

struct MetalGpuTimerRecord {
    const char* label = nullptr;
    uint32_t start_sample = 0;
    uint32_t end_sample = 0;
    bool ended = false;
};

struct MetalCommandList {
    MetalContext* context = nullptr;
    uint32_t pool_index = 0;
    id<MTLCommandBuffer> command_buffer = nil;
    id<MTLRenderCommandEncoder> render_encoder = nil;
    id<MTLComputeCommandEncoder> compute_encoder = nil;
    MTLRenderPassDescriptor* active_render_pass_descriptor = nil;
    Handle<RenderTarget> current_render_target;
    uint32_t render_target_width = 0;
    uint32_t render_target_height = 0;
    CommandStats stats{};

    Handle<Pipeline> bound_pipeline;
    Handle<Buffer> bound_vertex_buffer;
    uint32_t bound_vertex_stride = 0;
    uint32_t bound_vertex_offset = 0;
    Handle<Buffer> bound_index_buffer;
    uint32_t bound_index_size = 0;
    bool stencil_ref_set = false;
    uint8_t dynamic_stencil_ref = 0;
    bool bindless_bound_in_pass = false;

    uint32_t timestamp_sample_count = 0;
    uint32_t gpu_timer_count = 0;
    std::array<MetalGpuTimerRecord, kMetalMaxGpuTimersPerFrame> gpu_timers{};

    struct BoundResources {
        Handle<Pipeline> pipeline;
        UniformSpan uniforms;
        StorageSpan storages[5];
        UniformSpan uniforms2;
        Handle<Texture> texture;
        TextureFilter texture_filter = TextureFilter::Linear;
        bool compute = false;
        bool valid = false;
    };
    BoundResources bound_resources{};

    struct StorageDescriptorKey {
        Handle<Buffer> buffer;
        uint64_t address = 0;
        uint64_t range = 0;
    };
    std::array<StorageDescriptorKey, 5> storage_descriptor_keys{};

    bool pipeline_bind_failed = false;
    bool vertex_bind_failed = false;
    bool index_bind_failed = false;
    bool resource_bind_failed = false;
    bool recording = false;
    bool in_render_pass = false;
    bool rendered = false;
};

struct MetalPerFrame {
    MetalCommandList cmds;
    id<MTLCommandBuffer> last_command_buffer = nil;
    id<MTLCounterSampleBuffer> timestamp_sample_buffer = nil;
    bool has_completed_submission = false;
};

struct MetalContext {
    MetalCore* core = nullptr;
    ResourceStats resource_stats{};
    bool trace_resources = false;
    SDL_Window* window = nullptr;
    SDL_MetalView metal_view = nullptr;
    CAMetalLayer* metal_layer = nil;
    id<CAMetalDrawable> current_drawable = nil;
    bool headless = false;

    uint32_t width = 0;
    uint32_t height = 0;
    MTLPixelFormat swapchain_format = MTLPixelFormatBGRA8Unorm;
    Handle<RenderTarget> swapchain_render_target;
    Handle<Texture> swapchain_depth_texture;
    Handle<RenderTarget> default_render_target;

    MetalPerFrame frames[kMetalFramesInFlight];
    uint32_t frame_index = 0;
    uint64_t frame_id = 0;

    // Virtual descriptor ring offset tracked for CommandStats parity and capacity tests.
    uint64_t descriptor_ring_offset[kMetalFramesInFlight] = {};

    id<MTLBuffer> uniform_ring[kMetalFramesInFlight] = {};
    void* uniform_ring_mapped[kMetalFramesInFlight] = {};
    uint64_t uniform_ring_offset[kMetalFramesInFlight] = {};
    Handle<Buffer> uniform_ring_handle[kMetalFramesInFlight];

    id<MTLBuffer> vertex_ring[kMetalFramesInFlight] = {};
    void* vertex_ring_mapped[kMetalFramesInFlight] = {};
    uint64_t vertex_ring_offset[kMetalFramesInFlight] = {};
    Handle<Buffer> vertex_ring_handle[kMetalFramesInFlight];

    CommandStats frame_stats{};
    bool gpu_timers_supported = false;
    id<MTLBuffer> timestamp_scratch_buffer = nil;
    std::vector<GpuTimerResult> completed_gpu_timer_results;

    // Tier-2 bindless argument buffer:
    //   [0..4095] MTLResourceID textures (slot 0 reserved as invalid sentinel)
    //   [4096]    MTLResourceID linear sampler
    id<MTLBuffer> bindless_texture_table = nil;
    MTLResourceID* bindless_texture_table_mapped = nullptr;
    uint32_t bindless_texture_capacity = 0;
    uint32_t bindless_texture_count = 0;
    std::vector<uint32_t> free_bindless_texture_slots;
    // Compact list of live bindless textures for single batched useResources: per render pass.
    // Pointer elements are non-owning; MetalTexture owns the strong ARC reference while live.
    std::vector<__unsafe_unretained id<MTLResource>> bindless_resident_textures;
    std::vector<Handle<Texture>> bindless_resident_handles;

    id<MTLSamplerState> linear_sampler = nil;
    id<MTLSamplerState> nearest_sampler = nil;

    Pool<Texture, MetalTexture> texture_pool_;
    Pool<RenderTarget, MetalRenderTarget> render_target_pool_;
};

#endif // __OBJC__

// Pure-C++ white-box test hooks (used by tests/render_gfx_resources.cpp when
// NW_GFX_BACKEND_METAL is enabled so test TUs remain pure C++20 without ARC/ObjC++).
namespace metal_test {

std::vector<uint8_t> read_texture_mips(Context* ctx, Handle<Texture> handle, size_t bytes_per_pixel, bool depth_aspect);
bool texture_is_uploaded(Context* ctx, Handle<Texture> handle) noexcept;
void inject_texture_extent(Context* ctx, Handle<Texture> handle, uint32_t width, uint32_t height) noexcept;
uint32_t min_uniform_buffer_alignment(Context* ctx) noexcept;
uint32_t min_storage_buffer_alignment(Context* ctx) noexcept;
void set_uniform_ring_required_bytes(Context* ctx, uint64_t value) noexcept;
void set_descriptor_ring_offset(Context* ctx, CommandList* cmd, uint64_t value) noexcept;
uint64_t get_descriptor_ring_offset(Context* ctx, CommandList* cmd) noexcept;
bool command_list_resource_bind_failed(CommandList* cmd) noexcept;
void set_buffer_core_null(Handle<Buffer> handle, bool clear_to_null, Context* restore_ctx) noexcept;
size_t reserve_all_but_one_buffer_handles(std::vector<Handle<Buffer>>& out);
void release_reserved_buffer_handles(const std::vector<Handle<Buffer>>& handles);
void get_live_buffer_allocations(Context* ctx, uint64_t& count, uint64_t& bytes) noexcept;

} // namespace metal_test

} // namespace nw::gfx
