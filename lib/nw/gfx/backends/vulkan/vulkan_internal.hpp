#pragma once

#include "../../gfx.hpp"

#define VK_NO_PROTOTYPES
#include "volk.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <array>
#include <cstdlib>
#include <string>
#include <vector>

#define GFX_CHECK(cond, ...)                                         \
    do {                                                             \
        if (!(cond)) {                                               \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,               \
                "GFX_CHECK failed (%s:%d): ", __FILE__, __LINE__);   \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); \
            std::abort();                                            \
        }                                                            \
    } while (0)

#define VK_CHECK(x)                                                          \
    do {                                                                     \
        VkResult err = x;                                                    \
        if (err != VK_SUCCESS) {                                             \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,                       \
                "Vulkan error %d at %s:%d", (int)(err), __FILE__, __LINE__); \
            std::abort();                                                    \
        }                                                                    \
    } while (0)

namespace nw::gfx {

// ============================================================================
// == Utilities ===============================================================
// ============================================================================

struct VulkanContext;
struct VulkanCore;

inline Context* as_gfx(VulkanContext* value) { return reinterpret_cast<Context*>(value); }
inline VulkanContext* as_vulkan(Context* value) { return reinterpret_cast<VulkanContext*>(value); }
inline Core* as_gfx(VulkanCore* core) { return reinterpret_cast<Core*>(core); }
inline VulkanCore* as_vulkan(Core* core) { return reinterpret_cast<VulkanCore*>(core); }

inline VkFormat to_vk_format(Fmt fmt)
{
    switch (fmt) {
    case Fmt::RGBA8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Fmt::RGBA8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Fmt::RGBA16F:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Fmt::D32FS8:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Fmt::D32F:
        return VK_FORMAT_D32_SFLOAT;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

inline VkImageAspectFlags image_aspect_flags(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// ============================================================================
// == Core ====================================================================
// ============================================================================

struct VulkanCore {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    uint32_t graphics_queue_family = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceVulkan13Features features_13{};
    VkPhysicalDeviceVulkan12Features features_12{};
    VkPhysicalDeviceDescriptorIndexingFeatures indexing_features{};
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_props{};
    uint32_t graphics_queue_timestamp_valid_bits = 0;
    ValidationReport validation_report{};

    std::vector<const char*> enabled_device_extensions;
    std::vector<const char*> enabled_instance_extensions;
};

struct VulkanImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    uint32_t mip_levels = 1;
    uint32_t bindless_slot = 0;
    bool owned = true;
};

struct VulkanRenderTarget {
    RenderTargetDesc desc{};
};

struct VulkanBuffer {
    VulkanCore* core = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceAddress device_address = 0;
    size_t size = 0;
    bool cpu_visible = false;
};

extern Pool<Buffer, VulkanBuffer> g_buffer_pool;

struct VulkanShader {
    VulkanCore* core = nullptr;
    VkShaderModule module = VK_NULL_HANDLE;
};

struct VulkanPipeline {
    VulkanCore* core = nullptr;
    VkDescriptorSetLayout set0_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout texture_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

    bool uses_draw_uniforms = true;
    bool uses_draw_uniforms2 = false;
    bool uses_single_texture = true;
    bool uses_bindless_sampled_textures = false;
    bool uses_storage_buffer = false;
    uint8_t storage_buffer_count = 0;
    bool owns_texture_layout = true;

    // Descriptor buffer layout sizes and binding offsets
    VkDeviceSize set0_size = 0;
    VkDeviceSize set0_uniform_offset = 0;
    VkDeviceSize set0_uniform2_offset = 0;
    VkDeviceSize set0_storage_offsets[5] = {};
    VkDeviceSize texture_set_size = 0;
    VkDeviceSize texture_binding_offset = 0;
    VkDeviceSize sampler_binding_offset = 0;
};

extern Pool<Shader, VulkanShader> g_shader_pool;
extern Pool<Pipeline, VulkanPipeline> g_pipeline_pool;

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 16;
constexpr uint32_t MAX_GPU_TIMERS_PER_FRAME = 64;
constexpr uint32_t MAX_GPU_TIMESTAMP_QUERIES_PER_FRAME = MAX_GPU_TIMERS_PER_FRAME * 2;

struct VulkanGpuTimerRecord {
    const char* label = nullptr;
    uint32_t start_query = 0;
    uint32_t end_query = 0;
    bool ended = false;
};

struct VulkanCommandList {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer buffer = VK_NULL_HANDLE;
    VulkanContext* context = nullptr;
    uint32_t pool_index = 0;
    Handle<RenderTarget> current_render_target;
    CommandStats stats{};
    Handle<Pipeline> bound_pipeline;
    uint32_t timestamp_query_count = 0;
    uint32_t gpu_timer_count = 0;
    std::array<VulkanGpuTimerRecord, MAX_GPU_TIMERS_PER_FRAME> gpu_timers{};
    Handle<Buffer> bound_vertex_buffer;
    uint32_t bound_vertex_stride = 0;
    uint32_t bound_vertex_offset = 0;
    Handle<Buffer> bound_index_buffer;
    uint32_t bound_index_size = 0;
    bool descriptor_buffers_bound = false;
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
    BoundResources bound_resources;
    bool recording = false;
    bool in_render_pass = false;
    bool rendered = false;
};

struct PerFrame {
    VulkanCommandList cmds;
    VkSemaphore image_acquired = VK_NULL_HANDLE; // signaled by swapchain when image is ready to render into
    VkFence in_flight = VK_NULL_HANDLE;
    VkQueryPool timestamp_query_pool = VK_NULL_HANDLE;
};

struct VulkanContext {
    VulkanCore* core = nullptr;
    SDL_Window* window = nullptr;
    bool headless = false;

    uint32_t width = 0;
    uint32_t height = 0;

    // Swapchain-related
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    Handle<RenderTarget> swapchain_render_targets[MAX_SWAPCHAIN_IMAGES];
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkImage swapchain_images[MAX_SWAPCHAIN_IMAGES] = {};
    VkImageView swapchain_views[MAX_SWAPCHAIN_IMAGES] = {};
    VkImageLayout swapchain_layouts[MAX_SWAPCHAIN_IMAGES] = {};
    VkImageView swapchain_depth_views[MAX_SWAPCHAIN_IMAGES] = {}; // Depth/stencil views per swapchain image
    uint32_t swapchain_image_count = 0;
    uint32_t current_image_index = 0;
    bool swapchain_stale = false;

    PerFrame frames[MAX_FRAMES_IN_FLIGHT];
    // One render_finished semaphore per swapchain image — prevents reuse before the
    // present engine releases it (which happens on the image's next acquire, not on the fence).
    VkSemaphore render_finished[MAX_SWAPCHAIN_IMAGES] = {};
    uint32_t frame_index = 0;
    uint64_t frame_id = 0;

    // Per-frame descriptor ring buffers (replace descriptor pools)
    VkBuffer descriptor_ring[MAX_FRAMES_IN_FLIGHT] = {};
    VmaAllocation descriptor_ring_alloc[MAX_FRAMES_IN_FLIGHT] = {};
    void* descriptor_ring_mapped[MAX_FRAMES_IN_FLIGHT] = {};
    VkDeviceAddress descriptor_ring_addr[MAX_FRAMES_IN_FLIGHT] = {};
    VkDeviceSize descriptor_ring_offset[MAX_FRAMES_IN_FLIGHT] = {};

    VkBuffer uniform_ring[MAX_FRAMES_IN_FLIGHT] = {};
    VmaAllocation uniform_ring_alloc[MAX_FRAMES_IN_FLIGHT] = {};
    void* uniform_ring_mapped[MAX_FRAMES_IN_FLIGHT] = {};
    VkDeviceAddress uniform_ring_addr[MAX_FRAMES_IN_FLIGHT] = {};
    VkDeviceSize uniform_ring_offset[MAX_FRAMES_IN_FLIGHT] = {};
    Handle<Buffer> uniform_ring_handle[MAX_FRAMES_IN_FLIGHT];

    VkBuffer vertex_ring[MAX_FRAMES_IN_FLIGHT] = {};
    VmaAllocation vertex_ring_alloc[MAX_FRAMES_IN_FLIGHT] = {};
    void* vertex_ring_mapped[MAX_FRAMES_IN_FLIGHT] = {};
    VkDeviceAddress vertex_ring_addr[MAX_FRAMES_IN_FLIGHT] = {};
    VkDeviceSize vertex_ring_offset[MAX_FRAMES_IN_FLIGHT] = {};
    Handle<Buffer> vertex_ring_handle[MAX_FRAMES_IN_FLIGHT];
    CommandStats frame_stats{};
    bool gpu_timers_supported = false;
    float gpu_timestamp_period_ns = 0.0f;
    std::vector<GpuTimerResult> completed_gpu_timer_results;
    std::vector<uint64_t> timestamp_query_results;

    VkBuffer bindless_texture_table = VK_NULL_HANDLE;
    VmaAllocation bindless_texture_table_alloc = VK_NULL_HANDLE;
    void* bindless_texture_table_mapped = nullptr;
    VkDeviceAddress bindless_texture_table_addr = 0;
    uint32_t bindless_texture_capacity = 0;
    uint32_t bindless_texture_count = 0;
    std::vector<uint32_t> free_bindless_texture_slots;
    VkDescriptorSetLayout bindless_texture_layout = VK_NULL_HANDLE;
    VkDeviceSize bindless_texture_set_size = 0;
    VkDeviceSize bindless_texture_binding_offset = 0;
    VkDeviceSize bindless_sampler_binding_offset = 0;

    VkSampler linear_sampler = VK_NULL_HANDLE;
    VkSampler nearest_sampler = VK_NULL_HANDLE;

    // Default RT (headless)
    Handle<RenderTarget> default_render_target;

    // Pools
    Pool<Texture, VulkanImage> texture_pool_;
    Pool<RenderTarget, VulkanRenderTarget> render_target_pool_;
};

inline void begin_command_buffer(VulkanContext* ctx, uint32_t frame_index)
{
    PerFrame& frame = ctx->frames[frame_index];

    if (frame.cmds.pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = ctx->core->graphics_queue_family;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(ctx->core->device, &pool_info, nullptr, &frame.cmds.pool));

        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = frame.cmds.pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(ctx->core->device, &alloc_info, &frame.cmds.buffer));
    }

    vkResetCommandBuffer(frame.cmds.buffer, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.cmds.buffer, &begin_info));

    frame.cmds.context = ctx;
    frame.cmds.pool_index = frame_index;
    frame.cmds.current_render_target = {};
    frame.cmds.stats = {};
    frame.cmds.bound_pipeline = {};
    frame.cmds.timestamp_query_count = 0;
    frame.cmds.gpu_timer_count = 0;
    frame.cmds.gpu_timers = {};
    frame.cmds.bound_vertex_buffer = {};
    frame.cmds.bound_vertex_stride = 0;
    frame.cmds.bound_vertex_offset = 0;
    frame.cmds.bound_index_buffer = {};
    frame.cmds.bound_index_size = 0;
    frame.cmds.descriptor_buffers_bound = false;
    frame.cmds.bound_resources = {};
    frame.cmds.recording = true;
    frame.cmds.in_render_pass = false;
    frame.cmds.rendered = false;

    if (ctx->gpu_timers_supported && frame.timestamp_query_pool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.cmds.buffer, frame.timestamp_query_pool, 0, MAX_GPU_TIMESTAMP_QUERIES_PER_FRAME);
    }
}

} // namespace nw::gfx
