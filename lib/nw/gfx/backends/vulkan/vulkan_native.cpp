#include "../../native_vulkan.hpp"

#include "vulkan_internal.hpp"

namespace nw::gfx {

bool get_native_vulkan_core(Core* core, NativeVulkanCore& out) noexcept
{
    const auto* vk = as_vulkan(core);
    if (!vk) {
        return false;
    }

    out.instance = vk->instance;
    out.physical_device = vk->physical_device;
    out.device = vk->device;
    out.graphics_queue = vk->graphics_queue;
    out.graphics_queue_family = vk->graphics_queue_family;
    out.allocator = vk->allocator;
    return true;
}

bool get_native_vulkan_frame(Context* ctx, CommandList* cmd, NativeVulkanFrame& out) noexcept
{
    const auto* vk = as_vulkan(ctx);
    if (!vk || !cmd) {
        return false;
    }

    const auto* cmd_vk = reinterpret_cast<const VulkanCommandList*>(cmd);
    if (!cmd_vk || cmd_vk->buffer == VK_NULL_HANDLE) {
        return false;
    }

    out.command_buffer = cmd_vk->buffer;
    out.swapchain_format = vk->swapchain_format;
    out.frame_index = vk->frame_index;
    out.frame_id = vk->frame_id;
    out.image_index = vk->current_image_index;
    out.width = vk->width;
    out.height = vk->height;
    out.headless = vk->headless;

    if (!vk->headless && vk->current_image_index < vk->swapchain_image_count) {
        out.swapchain_image = reinterpret_cast<std::uint64_t>(vk->swapchain_images[vk->current_image_index]);
        out.swapchain_image_view = reinterpret_cast<std::uint64_t>(vk->swapchain_views[vk->current_image_index]);
    } else {
        out.swapchain_image = 0;
        out.swapchain_image_view = 0;
    }

    return true;
}

bool get_native_vulkan_buffer(Handle<Buffer> handle, NativeVulkanBuffer& out) noexcept
{
    const auto* buffer = g_buffer_pool.get(handle);
    if (!buffer || buffer->buffer == VK_NULL_HANDLE) {
        return false;
    }

    out.buffer = reinterpret_cast<std::uint64_t>(buffer->buffer);
    out.size = static_cast<std::uint64_t>(buffer->size);
    out.device_address = static_cast<std::uint64_t>(buffer->device_address);
    return true;
}

bool get_native_vulkan_texture(Context* ctx, Handle<Texture> handle, NativeVulkanTexture& out) noexcept
{
    auto* vk = as_vulkan(ctx);
    if (!vk) {
        return false;
    }

    auto* tex = vk->texture_pool_.get(handle);
    if (!tex || tex->image == VK_NULL_HANDLE || tex->view == VK_NULL_HANDLE) {
        return false;
    }

    out.image = reinterpret_cast<std::uint64_t>(tex->image);
    out.image_view = reinterpret_cast<std::uint64_t>(tex->view);
    out.format = tex->format;
    out.width = tex->width;
    out.height = tex->height;
    return true;
}

} // namespace nw::gfx
