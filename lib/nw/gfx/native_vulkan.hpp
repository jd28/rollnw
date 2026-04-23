#pragma once

#include "gfx.hpp"

#include <cstdint>

namespace nw::gfx {

typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
using VkBuffer = std::uint64_t;
using VkImage = std::uint64_t;
using VkImageView = std::uint64_t;

using VkFormat = std::uint32_t;

struct NativeVulkanCore {
    VkInstance instance = nullptr;
    VkPhysicalDevice physical_device = nullptr;
    VkDevice device = nullptr;
    VkQueue graphics_queue = nullptr;
    std::uint32_t graphics_queue_family = 0;
    void* allocator = nullptr;
};

struct NativeVulkanFrame {
    VkCommandBuffer command_buffer = nullptr;
    VkImage swapchain_image = 0;
    VkImageView swapchain_image_view = 0;
    VkFormat swapchain_format = 0;
    std::uint32_t frame_index = 0;
    std::uint64_t frame_id = 0;
    std::uint32_t image_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool headless = false;
};

struct NativeVulkanBuffer {
    VkBuffer buffer = 0;
    std::uint64_t size = 0;
    std::uint64_t device_address = 0;
};

struct NativeVulkanTexture {
    VkImage image = 0;
    VkImageView image_view = 0;
    VkFormat format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

bool get_native_vulkan_core(Core* core, NativeVulkanCore& out) noexcept;
bool get_native_vulkan_frame(Context* ctx, CommandList* cmd, NativeVulkanFrame& out) noexcept;
bool get_native_vulkan_buffer(Handle<Buffer> handle, NativeVulkanBuffer& out) noexcept;
bool get_native_vulkan_texture(Context* ctx, Handle<Texture> handle, NativeVulkanTexture& out) noexcept;

} // namespace nw::gfx
