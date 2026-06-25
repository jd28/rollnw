#include "../../../util/profile.hpp"
#include "../../gfx.hpp"
#include "vulkan_internal.hpp"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace nw::gfx {

namespace {

constexpr VkDeviceSize kDescriptorRingSize = 512 * 1024;
constexpr VkDeviceSize kUniformRingSize = 16 * 1024 * 1024;
constexpr VkDeviceSize kVertexRingSize = 16 * 1024 * 1024;
constexpr uint32_t kBindlessTextureCapacity = 4096;
constexpr double kNanosecondsToSeconds = 1.0e-9;

uint32_t graphics_storage_binding_for_slot(uint32_t slot) noexcept
{
    // Graphics pipelines reserve b4 for the optional secondary uniform span.
    // Keep storage slots 0/1 on the existing t2/t3 bindings and place later
    // storage slots after b4.
    return slot < 2 ? 2u + slot : 5u + (slot - 2u);
}

struct LegacyUiVertex {
    float position[2];
    uint32_t colour;
    float tex_coord[2];
};

bool create_persistent_buffer(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage,
    VkBuffer& buffer, VmaAllocation& allocation, void*& mapped, VkDeviceAddress& address)
{
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info{};
    if (vmaCreateBuffer(ctx->core->allocator, &info, &alloc, &buffer, &allocation, &alloc_info)
        != VK_SUCCESS) {
        return false;
    }

    mapped = alloc_info.pMappedData;

    VkBufferDeviceAddressInfo addr_info{};
    addr_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addr_info.buffer = buffer;
    address = vkGetBufferDeviceAddress(ctx->core->device, &addr_info);
    return true;
}

void collect_completed_gpu_timer_results(VulkanContext* ctx, const PerFrame& frame)
{
    ctx->completed_gpu_timer_results.clear();
    if (!ctx->gpu_timers_supported || frame.timestamp_query_pool == VK_NULL_HANDLE) {
        return;
    }

    const auto& cmds = frame.cmds;
    if (cmds.timestamp_query_count == 0 || cmds.gpu_timer_count == 0) {
        return;
    }

    const uint32_t query_count = std::min(cmds.timestamp_query_count, MAX_GPU_TIMESTAMP_QUERIES_PER_FRAME);
    ctx->timestamp_query_results.resize(static_cast<size_t>(query_count) * 2u);
    const VkDeviceSize result_stride = sizeof(uint64_t) * 2u;
    const VkDeviceSize result_bytes =
        static_cast<VkDeviceSize>(ctx->timestamp_query_results.size() * sizeof(uint64_t));
    const VkResult result = vkGetQueryPoolResults(
        ctx->core->device,
        frame.timestamp_query_pool,
        0,
        query_count,
        result_bytes,
        ctx->timestamp_query_results.data(),
        result_stride,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS && result != VK_NOT_READY) {
        return;
    }

    ctx->completed_gpu_timer_results.reserve(cmds.gpu_timer_count);
    for (uint32_t i = 0; i < cmds.gpu_timer_count; ++i) {
        const auto& timer = cmds.gpu_timers[i];
        if (!timer.ended || timer.start_query >= query_count || timer.end_query >= query_count) {
            continue;
        }

        const size_t start_offset = static_cast<size_t>(timer.start_query) * 2u;
        const size_t end_offset = static_cast<size_t>(timer.end_query) * 2u;
        const uint64_t start_timestamp = ctx->timestamp_query_results[start_offset];
        const uint64_t start_available = ctx->timestamp_query_results[start_offset + 1u];
        const uint64_t end_timestamp = ctx->timestamp_query_results[end_offset];
        const uint64_t end_available = ctx->timestamp_query_results[end_offset + 1u];
        GpuTimerResult timer_result{
            .label = timer.label,
            .seconds = 0.0f,
            .available = start_available != 0u && end_available != 0u && end_timestamp >= start_timestamp,
        };
        if (timer_result.available) {
            const double elapsed_ns =
                static_cast<double>(end_timestamp - start_timestamp) * static_cast<double>(ctx->gpu_timestamp_period_ns);
            timer_result.seconds = static_cast<float>(elapsed_ns * kNanosecondsToSeconds);
        }
        ctx->completed_gpu_timer_results.push_back(timer_result);
    }
}

void destroy_persistent_buffer(VulkanContext* ctx, VkBuffer& buffer, VmaAllocation& allocation)
{
    if (buffer == VK_NULL_HANDLE) {
        return;
    }
    vmaDestroyBuffer(ctx->core->allocator, buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

void write_bindless_texture_descriptor(VulkanContext* ctx, uint32_t slot, const VulkanImage* tex)
{
    if (!ctx->bindless_texture_table_mapped || !bindless_texture_index_valid(slot)
        || slot >= ctx->bindless_texture_capacity) {
        return;
    }

    VkDescriptorImageInfo img_info{};
    if (tex && tex->view != VK_NULL_HANDLE) {
        img_info.imageView = tex->view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkDescriptorGetInfoEXT tex_desc{};
    tex_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    tex_desc.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    tex_desc.data.pSampledImage = &img_info;

    auto* out = static_cast<uint8_t*>(ctx->bindless_texture_table_mapped)
        + ctx->bindless_texture_binding_offset
        + static_cast<size_t>(slot) * ctx->core->descriptor_buffer_props.sampledImageDescriptorSize;
    vkGetDescriptorEXT(ctx->core->device, &tex_desc,
        ctx->core->descriptor_buffer_props.sampledImageDescriptorSize, out);
}

void write_bindless_sampler_descriptor(VulkanContext* ctx)
{
    if (!ctx->bindless_texture_table_mapped || ctx->linear_sampler == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorGetInfoEXT sampler_desc{};
    sampler_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    sampler_desc.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sampler_desc.data.pSampler = &ctx->linear_sampler;

    auto* out = static_cast<uint8_t*>(ctx->bindless_texture_table_mapped)
        + ctx->bindless_sampler_binding_offset;
    vkGetDescriptorEXT(ctx->core->device, &sampler_desc,
        ctx->core->descriptor_buffer_props.samplerDescriptorSize, out);
}

} // namespace

static VkDeviceSize align_up(VkDeviceSize val, VkDeviceSize alignment)
{
    return (val + alignment - 1) & ~(alignment - 1);
}

Pool<Buffer, VulkanBuffer> g_buffer_pool;
Pool<Shader, VulkanShader> g_shader_pool;
Pool<Pipeline, VulkanPipeline> g_pipeline_pool;

// SDL handles X11/Wayland/Win32/Metal surface creation — no platform ifdefs needed.
VkSurfaceKHR create_surface(VulkanCore* core, SDL_Window* window)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    GFX_CHECK(SDL_Vulkan_CreateSurface(window, core->instance, nullptr, &surface),
        "SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
    return surface;
}

inline VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    uint32_t format_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr));

    std::vector<VkSurfaceFormatKHR> formats(format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data()));

    // Prefer VK_FORMAT_B8G8R8A8_UNORM with SRGB nonlinear
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }

    // Fallback to first
    return formats[0];
}

VkPresentModeKHR choose_present_mode(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    uint32_t mode_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, nullptr));

    std::vector<VkPresentModeKHR> modes(mode_count);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, modes.data()));

    // Prefer mailbox if available
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    // Fallback to FIFO which is guaranteed
    return VK_PRESENT_MODE_FIFO_KHR;
}

static bool is_depth_format(VkFormat fmt)
{
    return fmt == VK_FORMAT_D32_SFLOAT
        || fmt == VK_FORMAT_D16_UNORM
        || fmt == VK_FORMAT_D24_UNORM_S8_UINT
        || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static bool has_stencil_component(VkFormat fmt)
{
    return fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static VkBufferUsageFlags to_vk_buffer_usage(BufferUsage usage)
{
    switch (usage) {
    case BufferUsage::Vertex:
        return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case BufferUsage::Index:
        return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case BufferUsage::Uniform:
        return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case BufferUsage::Storage:
        return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case BufferUsage::TransferSrc:
        return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    case BufferUsage::TransferDst:
        return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    case BufferUsage::Indirect:
        return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    return 0;
}

static VkStencilOp to_vk_stencil_op(StencilOp op)
{
    switch (op) {
    case StencilOp::Keep:
        return VK_STENCIL_OP_KEEP;
    case StencilOp::Zero:
        return VK_STENCIL_OP_ZERO;
    case StencilOp::Replace:
        return VK_STENCIL_OP_REPLACE;
    case StencilOp::IncClamp:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::DecClamp:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::Invert:
        return VK_STENCIL_OP_INVERT;
    }
    return VK_STENCIL_OP_KEEP;
}

static VkCompareOp to_vk_compare_op(CompareOp op)
{
    switch (op) {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::AlwaysPass:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

static VkShaderStageFlags pipeline_stage_flags(VkPipelineBindPoint bind_point)
{
    return bind_point == VK_PIPELINE_BIND_POINT_COMPUTE
        ? VK_SHADER_STAGE_COMPUTE_BIT
        : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
}

static void transition_image_layout(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags src_access,
    VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage,
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static VkAccessFlags layout_access_mask(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return 0;
    default:
        return 0;
    }
}

static VkPipelineStageFlags layout_stage_mask(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    default:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

Handle<Buffer> create_buffer(Context* ctx, const BufferDesc& desc)
{
    auto* c = as_vulkan(ctx);

    VulkanBuffer buffer{};
    buffer.core = c->core;
    buffer.size = desc.size;
    buffer.cpu_visible = desc.cpu_visible;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = desc.size;
    info.usage = to_vk_buffer_usage(desc.usage) | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = desc.cpu_visible ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (desc.cpu_visible) {
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo alloc_info{};
    VK_CHECK(vmaCreateBuffer(c->core->allocator, &info, &alloc, &buffer.buffer, &buffer.allocation, &alloc_info));
    if (desc.cpu_visible) {
        buffer.mapped = alloc_info.pMappedData;
    }

    VkBufferDeviceAddressInfo addr_info{};
    addr_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addr_info.buffer = buffer.buffer;
    buffer.device_address = vkGetBufferDeviceAddress(c->core->device, &addr_info);

    return g_buffer_pool.insert(std::move(buffer));
}

void* map_buffer(Handle<Buffer> handle)
{
    auto* buffer = g_buffer_pool.get(handle);
    if (!buffer) {
        return nullptr;
    }

    if (!buffer->mapped) {
        VK_CHECK(vmaMapMemory(buffer->core->allocator, buffer->allocation, &buffer->mapped));
    }
    return buffer->mapped;
}

void unmap_buffer(Handle<Buffer> handle)
{
    auto* buffer = g_buffer_pool.get(handle);
    if (!buffer || !buffer->mapped) {
        return;
    }

    // For cpu_visible buffers, we keep the mapping persistent (VMA_ALLOCATION_CREATE_MAPPED_BIT).
    // This is intentional for performance - avoiding map/unmap overhead per frame.
    // Only unmap non-cpu_visible buffers that were explicitly mapped.
    if (!buffer->cpu_visible) {
        vmaUnmapMemory(buffer->core->allocator, buffer->allocation);
        buffer->mapped = nullptr;
    }
}

void destroy_buffer(Handle<Buffer> handle)
{
    auto* buffer = g_buffer_pool.get(handle);
    if (!buffer) {
        return;
    }

    if (buffer->mapped && !buffer->cpu_visible) {
        vmaUnmapMemory(buffer->core->allocator, buffer->allocation);
    }
    vmaDestroyBuffer(buffer->core->allocator, buffer->buffer, buffer->allocation);
    g_buffer_pool.destroy(handle);
}

Handle<Shader> create_shader(Context* ctx, const ShaderDesc& desc)
{
    auto* c = as_vulkan(ctx);
    if (!desc.code || desc.size == 0) {
        return {};
    }
    if ((desc.size % 4) != 0) {
        return {};
    }

    constexpr uint32_t k_spirv_magic = 0x07230203u;
    const auto* words = reinterpret_cast<const uint32_t*>(desc.code);
    if (words[0] != k_spirv_magic) {
        return {};
    }

    VulkanShader shader{};
    shader.core = c->core;

    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = desc.size;
    module_info.pCode = reinterpret_cast<const uint32_t*>(desc.code);
    VK_CHECK(vkCreateShaderModule(c->core->device, &module_info, nullptr, &shader.module));

    return g_shader_pool.insert(std::move(shader));
}

void destroy_shader(Context* ctx, Handle<Shader> handle)
{
    (void)ctx;
    auto* shader = g_shader_pool.get(handle);
    if (!shader) {
        return;
    }

    vkDestroyShaderModule(shader->core->device, shader->module, nullptr);
    g_shader_pool.destroy(handle);
}

Handle<Pipeline> create_pipeline(Context* ctx, const PipelineDesc& desc)
{
    auto* c = as_vulkan(ctx);
    auto* vs = g_shader_pool.get(desc.vs);
    auto* fs = g_shader_pool.get(desc.fs);
    if (!vs || !fs) {
        return {};
    }

    VulkanPipeline pipeline{};
    pipeline.core = c->core;
    pipeline.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    pipeline.uses_draw_uniforms = desc.uses_draw_uniforms;
    pipeline.uses_draw_uniforms2 = desc.uses_draw_uniforms2;
    pipeline.storage_buffer_count = std::min<uint8_t>(desc.storage_buffer_count, 5);
    if (pipeline.storage_buffer_count == 0 && desc.uses_storage_buffer) {
        pipeline.storage_buffer_count = 1;
    }
    pipeline.uses_storage_buffer = pipeline.storage_buffer_count != 0;
    pipeline.uses_single_texture = desc.uses_single_texture;
    pipeline.uses_bindless_sampled_textures = desc.uses_bindless_sampled_textures;

    if (pipeline.uses_single_texture && pipeline.uses_bindless_sampled_textures) {
        return {};
    }

    if (pipeline.uses_draw_uniforms || pipeline.uses_draw_uniforms2 || pipeline.uses_storage_buffer) {
        std::vector<VkDescriptorSetLayoutBinding> set0_bindings;
        const auto stage_flags = pipeline_stage_flags(pipeline.bind_point);
        if (pipeline.uses_draw_uniforms) {
            VkDescriptorSetLayoutBinding set0_binding{};
            set0_binding.binding = 1;
            set0_binding.descriptorCount = 1;
            set0_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            set0_binding.stageFlags = stage_flags;
            set0_bindings.push_back(set0_binding);
        }

        if (pipeline.uses_draw_uniforms2) {
            VkDescriptorSetLayoutBinding set0_binding2{};
            set0_binding2.binding = 4;
            set0_binding2.descriptorCount = 1;
            set0_binding2.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            set0_binding2.stageFlags = stage_flags;
            set0_bindings.push_back(set0_binding2);
        }

        for (uint32_t i = 0; i < pipeline.storage_buffer_count; ++i) {
            VkDescriptorSetLayoutBinding storage_binding{};
            storage_binding.binding = graphics_storage_binding_for_slot(i);
            storage_binding.descriptorCount = 1;
            storage_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            storage_binding.stageFlags = stage_flags;
            set0_bindings.push_back(storage_binding);
        }

        VkDescriptorSetLayoutCreateInfo set0_info{};
        set0_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set0_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        set0_info.bindingCount = static_cast<uint32_t>(set0_bindings.size());
        set0_info.pBindings = set0_bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(c->core->device, &set0_info, nullptr, &pipeline.set0_layout));

        vkGetDescriptorSetLayoutSizeEXT(c->core->device, pipeline.set0_layout, &pipeline.set0_size);
        if (pipeline.uses_draw_uniforms) {
            vkGetDescriptorSetLayoutBindingOffsetEXT(c->core->device, pipeline.set0_layout, 1, &pipeline.set0_uniform_offset);
        }
        if (pipeline.uses_draw_uniforms2) {
            vkGetDescriptorSetLayoutBindingOffsetEXT(c->core->device, pipeline.set0_layout, 4, &pipeline.set0_uniform2_offset);
        }
        for (uint32_t i = 0; i < pipeline.storage_buffer_count; ++i) {
            vkGetDescriptorSetLayoutBindingOffsetEXT(c->core->device, pipeline.set0_layout,
                graphics_storage_binding_for_slot(i), &pipeline.set0_storage_offsets[i]);
        }
    }

    if (pipeline.uses_single_texture || pipeline.uses_bindless_sampled_textures) {
        if (pipeline.uses_bindless_sampled_textures) {
            pipeline.texture_layout = c->bindless_texture_layout;
            pipeline.texture_set_size = c->bindless_texture_set_size;
            pipeline.texture_binding_offset = c->bindless_texture_binding_offset;
            pipeline.sampler_binding_offset = c->bindless_sampler_binding_offset;
            pipeline.owns_texture_layout = false;
        } else {
            VkDescriptorSetLayoutBinding tex_binding{};
            tex_binding.binding = 2;
            tex_binding.descriptorCount = 1;
            tex_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            tex_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo tex_info{};
            tex_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            tex_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
            tex_info.bindingCount = 1;
            tex_info.pBindings = &tex_binding;
            VK_CHECK(vkCreateDescriptorSetLayout(c->core->device, &tex_info, nullptr, &pipeline.texture_layout));

            vkGetDescriptorSetLayoutSizeEXT(c->core->device, pipeline.texture_layout, &pipeline.texture_set_size);
            vkGetDescriptorSetLayoutBindingOffsetEXT(c->core->device, pipeline.texture_layout, 2, &pipeline.texture_binding_offset);
        }
    }

    VkDescriptorSetLayout set_layouts[] = {pipeline.set0_layout, pipeline.texture_layout};
    uint32_t set_layout_count = 0;
    if (pipeline.uses_draw_uniforms || pipeline.uses_draw_uniforms2 || pipeline.uses_storage_buffer) {
        set_layouts[set_layout_count++] = pipeline.set0_layout;
    }
    if (pipeline.uses_single_texture || pipeline.uses_bindless_sampled_textures) {
        if (!pipeline.uses_draw_uniforms) {
            set_layouts[set_layout_count] = pipeline.texture_layout;
        }
        ++set_layout_count;
    }

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = set_layout_count;
    layout_info.pSetLayouts = set_layouts;
    VK_CHECK(vkCreatePipelineLayout(c->core->device, &layout_info, nullptr, &pipeline.layout));

    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vs->module;
    shader_stages[0].pName = "main";
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = fs->module;
    shader_stages[1].pName = "main";

    // Vertex input — use caller-supplied layout when provided, else fall back to
    // the legacy 2D UI vertex layout retained for compatibility.
    auto vk_format = [](VertexFormat f) -> VkFormat {
        switch (f) {
        case VertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::UByte4Norm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::UByte4:
            return VK_FORMAT_R8G8B8A8_UINT;
        }
        return VK_FORMAT_UNDEFINED;
    };

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> custom_attrs;
    VkVertexInputAttributeDescription rml_attrs[3]{};

    const bool custom_layout = !desc.vertex_attributes.empty();
    if (custom_layout) {
        binding.stride = desc.vertex_stride;
        custom_attrs.resize(desc.vertex_attributes.size());
        for (size_t i = 0; i < desc.vertex_attributes.size(); ++i) {
            custom_attrs[i].location = desc.vertex_attributes[i].location;
            custom_attrs[i].binding = 0;
            custom_attrs[i].format = vk_format(desc.vertex_attributes[i].format);
            custom_attrs[i].offset = desc.vertex_attributes[i].offset;
        }
    } else {
        binding.stride = sizeof(LegacyUiVertex);
        rml_attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(LegacyUiVertex, position))};
        rml_attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(offsetof(LegacyUiVertex, colour))};
        rml_attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(LegacyUiVertex, tex_coord))};
    }

    VkPipelineVertexInputStateCreateInfo vertex_info{};
    vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_info.vertexBindingDescriptionCount = 1;
    vertex_info.pVertexBindingDescriptions = &binding;
    if (custom_layout) {
        vertex_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(custom_attrs.size());
        vertex_info.pVertexAttributeDescriptions = custom_attrs.data();
    } else {
        vertex_info.vertexAttributeDescriptionCount = 3;
        vertex_info.pVertexAttributeDescriptions = rml_attrs;
    }

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = (desc.color_write && desc.blend_mode != BlendMode::disabled) ? VK_TRUE : VK_FALSE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    if (desc.blend_mode == BlendMode::additive) {
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    blend.colorWriteMask = desc.color_write ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0;

    VkPipelineColorBlendStateCreateInfo blend_state{};
    blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_state.attachmentCount = desc.has_color_attachment ? 1u : 0u;
    blend_state.pAttachments = desc.has_color_attachment ? &blend : nullptr;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = to_vk_compare_op(desc.depth_compare);
    depth_stencil.stencilTestEnable = desc.stencil_test ? VK_TRUE : VK_FALSE;
    depth_stencil.front.failOp = to_vk_stencil_op(desc.stencil_fail);
    depth_stencil.front.passOp = to_vk_stencil_op(desc.stencil_pass);
    depth_stencil.front.depthFailOp = to_vk_stencil_op(desc.stencil_depth_fail);
    depth_stencil.front.compareOp = to_vk_compare_op(desc.stencil_compare);
    depth_stencil.front.compareMask = desc.stencil_mask;
    depth_stencil.front.writeMask = desc.stencil_mask;
    depth_stencil.front.reference = desc.stencil_ref;
    depth_stencil.back = depth_stencil.front; // Same for back faces

    VkDynamicState dynamic_states[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = desc.stencil_test ? 3 : 2;
    dynamic.pDynamicStates = dynamic_states;

    const VkFormat color_format = (!c->headless && desc.use_swapchain_color_format)
        ? c->swapchain_format
        : to_vk_format(desc.color_attachment_format);
    const VkFormat depth_format = to_vk_format(desc.depth_attachment_format);
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = desc.has_color_attachment ? 1u : 0u;
    rendering.pColorAttachmentFormats = desc.has_color_attachment ? &color_format : nullptr;
    rendering.depthAttachmentFormat = depth_format;
    rendering.stencilAttachmentFormat = has_stencil_component(depth_format) ? depth_format : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering;
    pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_info;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState = &viewport;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &msaa;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &blend_state;
    pipeline_info.pDynamicState = &dynamic;
    pipeline_info.layout = pipeline.layout;
    VK_CHECK(vkCreateGraphicsPipelines(c->core->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.pipeline));

    return g_pipeline_pool.insert(std::move(pipeline));
}

Handle<Pipeline> create_compute_pipeline(Context* ctx, const ComputePipelineDesc& desc)
{
    auto* c = as_vulkan(ctx);
    auto* cs = g_shader_pool.get(desc.cs);
    if (!c || !cs) {
        return {};
    }

    VulkanPipeline pipeline{};
    pipeline.core = c->core;
    pipeline.bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    pipeline.uses_draw_uniforms = desc.uses_uniforms;
    pipeline.uses_single_texture = false;
    pipeline.uses_bindless_sampled_textures = false;
    pipeline.storage_buffer_count = std::min<uint8_t>(desc.storage_buffer_count, 5);
    if (pipeline.storage_buffer_count == 0 && desc.uses_storage_buffer) {
        pipeline.storage_buffer_count = 1;
    }
    pipeline.uses_storage_buffer = pipeline.storage_buffer_count != 0;

    if (pipeline.uses_draw_uniforms || pipeline.uses_storage_buffer) {
        std::vector<VkDescriptorSetLayoutBinding> set0_bindings;
        if (pipeline.uses_draw_uniforms) {
            VkDescriptorSetLayoutBinding uniform_binding{};
            uniform_binding.binding = 1;
            uniform_binding.descriptorCount = 1;
            uniform_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniform_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            set0_bindings.push_back(uniform_binding);
        }

        for (uint32_t i = 0; i < pipeline.storage_buffer_count; ++i) {
            VkDescriptorSetLayoutBinding storage_binding{};
            storage_binding.binding = graphics_storage_binding_for_slot(i);
            storage_binding.descriptorCount = 1;
            storage_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            storage_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            set0_bindings.push_back(storage_binding);
        }

        VkDescriptorSetLayoutCreateInfo set0_info{};
        set0_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set0_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        set0_info.bindingCount = static_cast<uint32_t>(set0_bindings.size());
        set0_info.pBindings = set0_bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(c->core->device, &set0_info, nullptr, &pipeline.set0_layout));

        vkGetDescriptorSetLayoutSizeEXT(c->core->device, pipeline.set0_layout, &pipeline.set0_size);
        if (pipeline.uses_draw_uniforms) {
            vkGetDescriptorSetLayoutBindingOffsetEXT(c->core->device, pipeline.set0_layout, 1, &pipeline.set0_uniform_offset);
        }
        for (uint32_t i = 0; i < pipeline.storage_buffer_count; ++i) {
            vkGetDescriptorSetLayoutBindingOffsetEXT(c->core->device, pipeline.set0_layout,
                graphics_storage_binding_for_slot(i), &pipeline.set0_storage_offsets[i]);
        }
    }

    VkDescriptorSetLayout set_layouts[] = {pipeline.set0_layout};
    uint32_t set_layout_count = 0;
    if (pipeline.uses_draw_uniforms || pipeline.uses_storage_buffer) {
        set_layouts[set_layout_count++] = pipeline.set0_layout;
    }

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = set_layout_count;
    layout_info.pSetLayouts = set_layouts;
    VK_CHECK(vkCreatePipelineLayout(c->core->device, &layout_info, nullptr, &pipeline.layout));

    VkPipelineShaderStageCreateInfo shader_stage{};
    shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shader_stage.module = cs->module;
    shader_stage.pName = "main";

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    pipeline_info.stage = shader_stage;
    pipeline_info.layout = pipeline.layout;
    VK_CHECK(vkCreateComputePipelines(c->core->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.pipeline));

    return g_pipeline_pool.insert(std::move(pipeline));
}

void destroy_pipeline(Context* ctx, Handle<Pipeline> handle)
{
    (void)ctx;
    auto* pipeline = g_pipeline_pool.get(handle);
    if (!pipeline) {
        return;
    }

    vkDestroyPipeline(pipeline->core->device, pipeline->pipeline, nullptr);
    vkDestroyPipelineLayout(pipeline->core->device, pipeline->layout, nullptr);
    if (pipeline->owns_texture_layout && pipeline->texture_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(pipeline->core->device, pipeline->texture_layout, nullptr);
    }
    if (pipeline->set0_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(pipeline->core->device, pipeline->set0_layout, nullptr);
    }
    g_pipeline_pool.destroy(handle);
}

Handle<Texture> create_texture(Context* ctx, const TextureDesc& desc)
{
    auto* c = as_vulkan(ctx);
    VulkanImage tex;
    tex.width = desc.width;
    tex.height = desc.height;
    tex.format = to_vk_format(desc.format);
    tex.mip_levels = desc.mip_levels;
    tex.layers = desc.layers;
    tex.owned = true;

    bool depth = is_depth_format(tex.format);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.sampled) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.storage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (desc.render_target) usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                           : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = tex.format;
    image_info.extent = {desc.width, desc.height, 1};
    image_info.mipLevels = desc.mip_levels;
    image_info.arrayLayers = desc.layers;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(c->core->allocator, &image_info, &alloc_info,
        &tex.image, &tex.allocation, nullptr));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = tex.image;
    view_info.viewType = (desc.layers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = tex.format;
    view_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    view_info.subresourceRange = {
        .aspectMask = nw::gfx::image_aspect_flags(tex.format),
        .baseMipLevel = 0,
        .levelCount = desc.mip_levels,
        .baseArrayLayer = 0,
        .layerCount = desc.layers,
    };

    VK_CHECK(vkCreateImageView(c->core->device, &view_info, nullptr, &tex.view));
    tex.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (desc.sampled) {
        if (!c->free_bindless_texture_slots.empty()) {
            tex.bindless_slot = c->free_bindless_texture_slots.back();
            c->free_bindless_texture_slots.pop_back();
        } else if (c->bindless_texture_count < c->bindless_texture_capacity) {
            tex.bindless_slot = c->bindless_texture_count++;
        }
    }

    auto handle = c->texture_pool_.insert(std::move(tex));
    if (auto* stored = c->texture_pool_.get(handle); stored && bindless_texture_index_valid(stored->bindless_slot)) {
        write_bindless_texture_descriptor(c, stored->bindless_slot, stored);
    }
    return handle;
}

void destroy_texture(Context* ctx_ptr, Handle<Texture> handle)
{
    auto* ctx = as_vulkan(ctx_ptr);
    auto* core = ctx->core;
    auto* tex = ctx->texture_pool_.get(handle);
    if (!tex) { return; }

    if (bindless_texture_index_valid(tex->bindless_slot)) {
        ctx->free_bindless_texture_slots.push_back(tex->bindless_slot);
    }

    vkDestroyImageView(core->device, tex->view, nullptr);

    if (tex->owned && tex->image != VK_NULL_HANDLE) {
        vmaDestroyImage(core->allocator, tex->image, tex->allocation);
    }

    ctx->texture_pool_.destroy(handle);
}

static bool upload_texture_pixels(Context* ctx_ptr, Handle<Texture> handle, const void* data, size_t size, size_t bytes_per_pixel)
{
    auto* ctx = as_vulkan(ctx_ptr);
    auto* tex = ctx->texture_pool_.get(handle);
    if (!tex || !data || tex->image == VK_NULL_HANDLE) {
        return false;
    }

    const size_t expected_size = static_cast<size_t>(tex->width) * static_cast<size_t>(tex->height) * bytes_per_pixel;
    if (size != expected_size) {
        return false;
    }

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = VK_NULL_HANDLE;

    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapped_info{};
    VkResult staging_result = vmaCreateBuffer(ctx->core->allocator, &staging_info, &alloc_info, &staging_buffer, &staging_alloc, &mapped_info);
    if (staging_result != VK_SUCCESS) {
        return false;
    }
    std::memcpy(mapped_info.pMappedData, data, size);

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = ctx->core->graphics_queue_family;
    VK_CHECK(vkCreateCommandPool(ctx->core->device, &pool_info, nullptr, &command_pool));

    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = command_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(ctx->core->device, &cmd_alloc, &command_buffer));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    const VkImageLayout source_layout = tex->layout;
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = tex->mip_levels;
    barrier.oldLayout = source_layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = tex->width;
    region.imageExtent.height = tex->height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(command_buffer, staging_buffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (tex->mip_levels > 1) {
        int32_t mip_width = static_cast<int32_t>(tex->width);
        int32_t mip_height = static_cast<int32_t>(tex->height);

        for (uint32_t level = 1; level < tex->mip_levels; ++level) {
            VkImageMemoryBarrier mip_barrier{};
            mip_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            mip_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mip_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mip_barrier.image = tex->image;
            mip_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mip_barrier.subresourceRange.baseArrayLayer = 0;
            mip_barrier.subresourceRange.layerCount = 1;
            mip_barrier.subresourceRange.baseMipLevel = level - 1;
            mip_barrier.subresourceRange.levelCount = 1;
            mip_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            mip_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            mip_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mip_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &mip_barrier);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = level - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.srcOffsets[1] = {mip_width, mip_height, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = level;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            blit.dstOffsets[1] = {std::max(1, mip_width / 2), std::max(1, mip_height / 2), 1};
            vkCmdBlitImage(command_buffer,
                tex->image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                tex->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &blit,
                VK_FILTER_LINEAR);

            mip_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            mip_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mip_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            mip_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &mip_barrier);

            mip_width = std::max(1, mip_width / 2);
            mip_height = std::max(1, mip_height / 2);
        }

        barrier.subresourceRange.baseMipLevel = tex->mip_levels - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);
    } else {
        transition_image_layout(command_buffer, tex->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(ctx->core->device, &fence_info, nullptr, &fence));

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    VK_CHECK(vkQueueSubmit(ctx->core->graphics_queue, 1, &submit, fence));

    // Use a 5-second timeout instead of UINT64_MAX to avoid infinite hangs
    constexpr uint64_t k_fence_timeout_ns = 5'000'000'000; // 5 seconds
    VkResult wait_result = vkWaitForFences(ctx->core->device, 1, &fence, VK_TRUE, k_fence_timeout_ns);
    if (wait_result != VK_SUCCESS) {
        vkDestroyFence(ctx->core->device, fence, nullptr);
        vkFreeCommandBuffers(ctx->core->device, command_pool, 1, &command_buffer);
        vkDestroyCommandPool(ctx->core->device, command_pool, nullptr);
        vmaDestroyBuffer(ctx->core->allocator, staging_buffer, staging_alloc);
        return false;
    }

    vkDestroyFence(ctx->core->device, fence, nullptr);
    vkFreeCommandBuffers(ctx->core->device, command_pool, 1, &command_buffer);
    vkDestroyCommandPool(ctx->core->device, command_pool, nullptr);
    vmaDestroyBuffer(ctx->core->allocator, staging_buffer, staging_alloc);

    tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (bindless_texture_index_valid(tex->bindless_slot)) {
        write_bindless_texture_descriptor(ctx, tex->bindless_slot, tex);
    }
    return true;
}

static bool upload_texture_pixels_mips(
    Context* ctx_ptr, Handle<Texture> handle, const TextureMipData* levels, size_t level_count, size_t bytes_per_pixel)
{
    auto* ctx = as_vulkan(ctx_ptr);
    auto* tex = ctx->texture_pool_.get(handle);
    if (!tex || !levels || tex->image == VK_NULL_HANDLE || level_count == 0 || tex->layers != 1) {
        return false;
    }
    if (tex->mip_levels != level_count) {
        return false;
    }

    size_t total_size = 0;
    uint32_t expected_width = tex->width;
    uint32_t expected_height = tex->height;
    for (size_t level = 0; level < level_count; ++level) {
        const auto& mip = levels[level];
        if (!mip.data || mip.width != expected_width || mip.height != expected_height) {
            return false;
        }
        const size_t expected_size = static_cast<size_t>(mip.width) * static_cast<size_t>(mip.height) * bytes_per_pixel;
        if (mip.size != expected_size) {
            return false;
        }
        total_size += mip.size;
        expected_width = std::max(1u, expected_width / 2u);
        expected_height = std::max(1u, expected_height / 2u);
    }

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = VK_NULL_HANDLE;

    VkBufferCreateInfo staging_info{};
    staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_info.size = total_size;
    staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapped_info{};
    VkResult staging_result = vmaCreateBuffer(ctx->core->allocator, &staging_info, &alloc_info, &staging_buffer, &staging_alloc, &mapped_info);
    if (staging_result != VK_SUCCESS) {
        return false;
    }

    size_t offset = 0;
    std::vector<VkBufferImageCopy> regions(level_count);
    for (size_t level = 0; level < level_count; ++level) {
        const auto& mip = levels[level];
        std::memcpy(static_cast<uint8_t*>(mapped_info.pMappedData) + offset, mip.data, mip.size);

        auto& region = regions[level];
        region.bufferOffset = offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = static_cast<uint32_t>(level);
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = mip.width;
        region.imageExtent.height = mip.height;
        region.imageExtent.depth = 1;
        offset += mip.size;
    }

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = ctx->core->graphics_queue_family;
    VK_CHECK(vkCreateCommandPool(ctx->core->device, &pool_info, nullptr, &command_pool));

    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = command_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(ctx->core->device, &cmd_alloc, &command_buffer));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    const VkImageLayout source_layout = tex->layout;
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = tex->mip_levels;
    barrier.oldLayout = source_layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

    vkCmdCopyBufferToImage(
        command_buffer, staging_buffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(regions.size()), regions.data());

    transition_image_layout(command_buffer, tex->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(ctx->core->device, &fence_info, nullptr, &fence));

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    VK_CHECK(vkQueueSubmit(ctx->core->graphics_queue, 1, &submit, fence));

    constexpr uint64_t k_fence_timeout_ns = 5'000'000'000;
    VkResult wait_result = vkWaitForFences(ctx->core->device, 1, &fence, VK_TRUE, k_fence_timeout_ns);
    if (wait_result != VK_SUCCESS) {
        vkDestroyFence(ctx->core->device, fence, nullptr);
        vkFreeCommandBuffers(ctx->core->device, command_pool, 1, &command_buffer);
        vkDestroyCommandPool(ctx->core->device, command_pool, nullptr);
        vmaDestroyBuffer(ctx->core->allocator, staging_buffer, staging_alloc);
        return false;
    }

    vkDestroyFence(ctx->core->device, fence, nullptr);
    vkFreeCommandBuffers(ctx->core->device, command_pool, 1, &command_buffer);
    vkDestroyCommandPool(ctx->core->device, command_pool, nullptr);
    vmaDestroyBuffer(ctx->core->allocator, staging_buffer, staging_alloc);

    tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (bindless_texture_index_valid(tex->bindless_slot)) {
        write_bindless_texture_descriptor(ctx, tex->bindless_slot, tex);
    }
    return true;
}

void cmd_begin_render(CommandList* cmd_ptr, Handle<RenderTarget> target, RenderLoadOp color_load_op)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx || cmd->in_render_pass) {
        return;
    }

    Handle<RenderTarget> render_target = target;
    if (!render_target.valid()) {
        render_target = ctx->headless ? ctx->default_render_target : ctx->swapchain_render_targets[ctx->current_image_index];
    }
    auto* rt = ctx->render_target_pool_.get(render_target);
    if (!rt) {
        return;
    }

    VulkanImage* color_tex = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    bool use_swapchain_color = false;
    const bool has_color_attachment = rt->desc.color[0].texture.valid();
    if (has_color_attachment) {
        color_tex = ctx->texture_pool_.get(rt->desc.color[0].texture);
        if (!color_tex) {
            return;
        }

        use_swapchain_color = !ctx->headless && color_tex->image == VK_NULL_HANDLE;
        image = use_swapchain_color ? ctx->swapchain_images[ctx->current_image_index] : color_tex->image;
        view = use_swapchain_color ? ctx->swapchain_views[ctx->current_image_index] : color_tex->view;
        const VkImageLayout old_layout = use_swapchain_color ? ctx->swapchain_layouts[ctx->current_image_index] : color_tex->layout;

        transition_image_layout(cmd->buffer, image,
            old_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            layout_access_mask(old_layout),
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
            layout_stage_mask(old_layout),
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        if (use_swapchain_color) {
            ctx->swapchain_layouts[ctx->current_image_index] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else {
            color_tex->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    // Transition depth/stencil image to attachment optimal
    VkImage depth_image = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VulkanImage* depth_tex = nullptr;
    if (rt && rt->desc.depth.texture.valid()) {
        depth_tex = ctx->texture_pool_.get(rt->desc.depth.texture);
        if (depth_tex) {
            depth_image = depth_tex->image;
            depth_view = depth_tex->view;
        }
    }
    if (depth_image != VK_NULL_HANDLE) {
        transition_image_layout(cmd->buffer, depth_image,
            depth_tex->layout,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            layout_access_mask(depth_tex->layout),
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            layout_stage_mask(depth_tex->layout),
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            image_aspect_flags(depth_tex->format));
        depth_tex->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo color_attachment{};
    if (has_color_attachment) {
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = view;
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = (color_load_op == RenderLoadOp::load || !rt->desc.color[0].clear)
            ? VK_ATTACHMENT_LOAD_OP_LOAD
            : VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue.color = {{0.24f, 0.30f, 0.42f, 1.0f}};
    }

    const VkImageView render_depth_view = use_swapchain_color
        ? ctx->swapchain_depth_views[ctx->current_image_index]
        : depth_view;
    if (use_swapchain_color && render_depth_view == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Missing swapchain depth attachment for image %u\n", ctx->current_image_index);
        return;
    }

    VkRenderingAttachmentInfo depth_stencil_attachment{};
    depth_stencil_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_stencil_attachment.imageView = render_depth_view;
    depth_stencil_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_stencil_attachment.loadOp = rt->desc.depth.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_stencil_attachment.storeOp = render_depth_view != VK_NULL_HANDLE ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_stencil_attachment.clearValue.depthStencil = {1.0f, 0}; // Clear depth to 1.0, stencil to 0

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.offset = {0, 0};
    rendering.renderArea.extent = {has_color_attachment ? color_tex->width : depth_tex->width,
        has_color_attachment ? color_tex->height : depth_tex->height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = has_color_attachment ? 1u : 0u;
    rendering.pColorAttachments = has_color_attachment ? &color_attachment : nullptr;
    rendering.pDepthAttachment = render_depth_view != VK_NULL_HANDLE ? &depth_stencil_attachment : nullptr;
    rendering.pStencilAttachment = (render_depth_view != VK_NULL_HANDLE && ((depth_tex && has_stencil_component(depth_tex->format)) || use_swapchain_color))
        ? &depth_stencil_attachment
        : nullptr;

    vkCmdBeginRendering(cmd->buffer, &rendering);
    cmd->current_render_target = render_target;
    cmd->in_render_pass = true;
    cmd->rendered = true;
    ++cmd->stats.render_pass_count;

    cmd->bound_pipeline = {};
    cmd->bound_vertex_buffer = {};
    cmd->bound_vertex_stride = 0;
    cmd->bound_vertex_offset = 0;
    cmd->bound_index_buffer = {};
    cmd->bound_index_size = 0;
    cmd->bound_resources = {};
    cmd->descriptor_buffers_bound = false;

    VkDescriptorBufferBindingInfoEXT desc_bindings[2]{};
    desc_bindings[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    desc_bindings[0].address = ctx->descriptor_ring_addr[cmd->pool_index];
    desc_bindings[0].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
        | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
    desc_bindings[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    desc_bindings[1].address = ctx->bindless_texture_table_addr;
    desc_bindings[1].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
        | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
    vkCmdBindDescriptorBuffersEXT(cmd->buffer, 2, desc_bindings);
    cmd->descriptor_buffers_bound = true;
    ++cmd->stats.descriptor_buffer_bind_count;
}

void cmd_end_render(CommandList* cmd_ptr)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd || !cmd->in_render_pass) {
        return;
    }

    auto* ctx = cmd->context;
    auto* rt = ctx ? ctx->render_target_pool_.get(cmd->current_render_target) : nullptr;
    vkCmdEndRendering(cmd->buffer);

    if (ctx && rt) {
        if (rt->desc.color[0].texture.valid()) {
            if (auto* color_tex = ctx->texture_pool_.get(rt->desc.color[0].texture)) {
                if (color_tex->image != VK_NULL_HANDLE) {
                    transition_image_layout(cmd->buffer, color_tex->image,
                        color_tex->layout,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        layout_access_mask(color_tex->layout),
                        VK_ACCESS_SHADER_READ_BIT,
                        layout_stage_mask(color_tex->layout),
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                    color_tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
            }
        }

        if (rt->desc.depth.texture.valid()) {
            if (auto* depth_tex = ctx->texture_pool_.get(rt->desc.depth.texture); depth_tex && depth_tex->image != VK_NULL_HANDLE) {
                transition_image_layout(cmd->buffer, depth_tex->image,
                    depth_tex->layout,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    layout_access_mask(depth_tex->layout),
                    VK_ACCESS_SHADER_READ_BIT,
                    layout_stage_mask(depth_tex->layout),
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    image_aspect_flags(depth_tex->format));
                depth_tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
    }

    cmd->current_render_target = {};
    cmd->in_render_pass = false;
}

GpuTimerScope cmd_begin_gpu_timer(CommandList* cmd_ptr, const char* label)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx || !cmd->recording || !ctx->gpu_timers_supported
        || ctx->frames[cmd->pool_index].timestamp_query_pool == VK_NULL_HANDLE
        || cmd->gpu_timer_count >= MAX_GPU_TIMERS_PER_FRAME
        || cmd->timestamp_query_count + 2u > MAX_GPU_TIMESTAMP_QUERIES_PER_FRAME) {
        return {};
    }

    const uint32_t timer_index = cmd->gpu_timer_count++;
    const uint32_t start_query = cmd->timestamp_query_count++;
    const uint32_t end_query = cmd->timestamp_query_count++;
    cmd->gpu_timers[timer_index] = VulkanGpuTimerRecord{
        .label = label,
        .start_query = start_query,
        .end_query = end_query,
        .ended = false,
    };
    vkCmdWriteTimestamp2(
        cmd->buffer,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        ctx->frames[cmd->pool_index].timestamp_query_pool,
        start_query);
    return GpuTimerScope{timer_index};
}

void cmd_end_gpu_timer(CommandList* cmd_ptr, GpuTimerScope scope)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx || !cmd->recording || !ctx->gpu_timers_supported || !scope.valid()
        || scope.index >= cmd->gpu_timer_count
        || ctx->frames[cmd->pool_index].timestamp_query_pool == VK_NULL_HANDLE) {
        return;
    }

    auto& timer = cmd->gpu_timers[scope.index];
    if (timer.ended) {
        return;
    }

    vkCmdWriteTimestamp2(
        cmd->buffer,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        ctx->frames[cmd->pool_index].timestamp_query_pool,
        timer.end_query);
    timer.ended = true;
}

void cmd_bind_pipeline(CommandList* cmd_ptr, Handle<Pipeline> handle)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* pipeline = g_pipeline_pool.get(handle);
    if (!cmd || !pipeline) {
        return;
    }
    if (cmd->bound_pipeline == handle) {
        ++cmd->stats.pipeline_bind_skipped_count;
        return;
    }
    vkCmdBindPipeline(cmd->buffer, pipeline->bind_point, pipeline->pipeline);
    cmd->bound_pipeline = handle;
    ++cmd->stats.pipeline_bind_count;
}

void cmd_bind_vertex_buffer(CommandList* cmd_ptr, Handle<Buffer> handle, uint32_t stride)
{
    cmd_bind_vertex_buffer(cmd_ptr, handle, stride, 0);
}

void cmd_bind_vertex_buffer(CommandList* cmd_ptr, Handle<Buffer> handle, uint32_t stride, uint32_t offset)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* buffer = g_buffer_pool.get(handle);
    if (!cmd || !buffer) {
        return;
    }
    if (cmd->bound_vertex_buffer == handle
        && cmd->bound_vertex_stride == stride
        && cmd->bound_vertex_offset == offset) {
        ++cmd->stats.vertex_buffer_bind_skipped_count;
        return;
    }
    VkDeviceSize vk_offset = offset;
    vkCmdBindVertexBuffers(cmd->buffer, 0, 1, &buffer->buffer, &vk_offset);
    cmd->bound_vertex_buffer = handle;
    cmd->bound_vertex_stride = stride;
    cmd->bound_vertex_offset = offset;
    ++cmd->stats.vertex_buffer_bind_count;
}

void cmd_bind_index_buffer(CommandList* cmd_ptr, Handle<Buffer> handle, uint32_t index_size)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* buffer = g_buffer_pool.get(handle);
    if (!cmd || !buffer) {
        return;
    }
    if (cmd->bound_index_buffer == handle && cmd->bound_index_size == index_size) {
        ++cmd->stats.index_buffer_bind_skipped_count;
        return;
    }

    VkIndexType index_type = index_size == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd->buffer, buffer->buffer, 0, index_type);
    cmd->bound_index_buffer = handle;
    cmd->bound_index_size = index_size;
    ++cmd->stats.index_buffer_bind_count;
}

void cmd_set_viewport(CommandList* cmd_ptr, float x, float y, float width, float height, float min_depth, float max_depth)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }

    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = min_depth;
    viewport.maxDepth = max_depth;
    vkCmdSetViewport(cmd->buffer, 0, 1, &viewport);
}

void cmd_set_scissor(CommandList* cmd_ptr, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }

    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd->buffer, 0, 1, &scissor);
}

void cmd_set_stencil_ref(CommandList* cmd_ptr, uint8_t ref)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    vkCmdSetStencilReference(cmd->buffer, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
}

static bool uniform_span_matches(const UniformSpan& lhs, const UniformSpan& rhs) noexcept
{
    return lhs.buffer == rhs.buffer
        && lhs.offset == rhs.offset
        && lhs.size == rhs.size;
}

static bool storage_span_matches(const StorageSpan& lhs, const StorageSpan& rhs) noexcept
{
    return lhs.buffer == rhs.buffer
        && lhs.offset == rhs.offset
        && lhs.size == rhs.size;
}

static bool resource_bind_matches(
    const VulkanCommandList::BoundResources& lhs,
    const VulkanCommandList::BoundResources& rhs) noexcept
{
    return lhs.valid
        && rhs.valid
        && lhs.compute == rhs.compute
        && lhs.pipeline == rhs.pipeline
        && uniform_span_matches(lhs.uniforms, rhs.uniforms)
        && storage_span_matches(lhs.storages[0], rhs.storages[0])
        && storage_span_matches(lhs.storages[1], rhs.storages[1])
        && storage_span_matches(lhs.storages[2], rhs.storages[2])
        && storage_span_matches(lhs.storages[3], rhs.storages[3])
        && storage_span_matches(lhs.storages[4], rhs.storages[4])
        && uniform_span_matches(lhs.uniforms2, rhs.uniforms2)
        && lhs.texture == rhs.texture
        && lhs.texture_filter == rhs.texture_filter;
}

static bool bind_descriptor_buffers(VulkanCommandList* cmd)
{
    auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx) {
        return false;
    }
    if (cmd->descriptor_buffers_bound) {
        ++cmd->stats.descriptor_buffer_bind_skipped_count;
        return true;
    }

    VkDescriptorBufferBindingInfoEXT desc_bindings[2]{};
    desc_bindings[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    desc_bindings[0].address = ctx->descriptor_ring_addr[cmd->pool_index];
    desc_bindings[0].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
        | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
    desc_bindings[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    desc_bindings[1].address = ctx->bindless_texture_table_addr;
    desc_bindings[1].usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
        | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
    vkCmdBindDescriptorBuffersEXT(cmd->buffer, 2, desc_bindings);
    cmd->descriptor_buffers_bound = true;
    ++cmd->stats.descriptor_buffer_bind_count;
    return true;
}

static bool bind_pipeline_resources(VulkanCommandList* cmd, VulkanPipeline* pipeline,
    const VulkanBuffer* uniform, uint32_t uniform_offset, uint32_t uniform_size,
    const StorageSpan& storage0, const StorageSpan& storage1, const StorageSpan& storage2,
    const StorageSpan& storage3, const StorageSpan& storage4, const VulkanImage* texture,
    TextureFilter texture_filter, const VulkanBuffer* uniform2, uint32_t uniform2_offset, uint32_t uniform2_size)
{
    auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx || !pipeline) {
        return false;
    }

    if (!bind_descriptor_buffers(cmd)) {
        return false;
    }

    const VkPhysicalDeviceDescriptorBufferPropertiesEXT& props = ctx->core->descriptor_buffer_props;
    const VkDeviceSize alignment = props.descriptorBufferOffsetAlignment;
    auto* ring = static_cast<uint8_t*>(ctx->descriptor_ring_mapped[cmd->pool_index]);
    VkDeviceSize& ring_offset = ctx->descriptor_ring_offset[cmd->pool_index];

    VkDeviceSize set_offsets[2] = {};
    uint32_t buffer_indices[2] = {};
    uint32_t set_count = 0;

    if (pipeline->uses_draw_uniforms) {
        if (!uniform) {
            return false;
        }

        const VkDeviceSize set0_offset = align_up(ring_offset, alignment);
        ring_offset = set0_offset + align_up(pipeline->set0_size, alignment);

        VkDescriptorAddressInfoEXT ub_addr{};
        ub_addr.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
        ub_addr.address = uniform->device_address + uniform_offset;
        ub_addr.range = uniform_size;

        VkDescriptorGetInfoEXT ub_desc{};
        ub_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        ub_desc.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ub_desc.data.pUniformBuffer = &ub_addr;

        vkGetDescriptorEXT(ctx->core->device, &ub_desc, props.uniformBufferDescriptorSize,
            ring + set0_offset + pipeline->set0_uniform_offset);

        buffer_indices[set_count] = 0;
        set_offsets[set_count] = set0_offset;
        ++set_count;
    }

    if (pipeline->uses_draw_uniforms2) {
        if (!uniform2) {
            return false;
        }

        const VkDeviceSize set0_offset = pipeline->uses_draw_uniforms
            ? set_offsets[0]
            : align_up(ring_offset, alignment);
        if (!pipeline->uses_draw_uniforms) {
            ring_offset = set0_offset + align_up(pipeline->set0_size, alignment);
        }

        VkDescriptorAddressInfoEXT ub_addr{};
        ub_addr.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
        ub_addr.address = uniform2->device_address + uniform2_offset;
        ub_addr.range = uniform2_size;

        VkDescriptorGetInfoEXT ub_desc{};
        ub_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
        ub_desc.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ub_desc.data.pUniformBuffer = &ub_addr;

        vkGetDescriptorEXT(ctx->core->device, &ub_desc, props.uniformBufferDescriptorSize,
            ring + set0_offset + pipeline->set0_uniform2_offset);

        if (!pipeline->uses_draw_uniforms) {
            buffer_indices[set_count] = 0;
            set_offsets[set_count] = set0_offset;
            ++set_count;
        }
    }

    if (pipeline->uses_storage_buffer) {
        const StorageSpan storages[5] = {storage0, storage1, storage2, storage3, storage4};
        for (uint32_t i = 0; i < pipeline->storage_buffer_count; ++i) {
            if (!storages[i].buffer.valid()) {
                return false;
            }
        }

        const VkDeviceSize set0_offset = pipeline->uses_draw_uniforms
            ? set_offsets[0]
            : align_up(ring_offset, alignment);
        if (!pipeline->uses_draw_uniforms) {
            ring_offset = set0_offset + align_up(pipeline->set0_size, alignment);
        }

        for (uint32_t i = 0; i < pipeline->storage_buffer_count; ++i) {
            auto* storage = g_buffer_pool.get(storages[i].buffer);
            if (!storage) {
                return false;
            }

            const VkDeviceSize storage_offset = storages[i].offset;
            if (storage_offset > storage->size) {
                return false;
            }
            const VkDeviceSize storage_range = storages[i].size != 0
                ? storages[i].size
                : (storage->size - storage_offset);
            if (storage_offset + storage_range > storage->size) {
                return false;
            }

            VkDescriptorAddressInfoEXT storage_addr{};
            storage_addr.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            storage_addr.address = storage->device_address + storage_offset;
            storage_addr.range = storage_range;

            VkDescriptorGetInfoEXT storage_desc{};
            storage_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            storage_desc.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            storage_desc.data.pStorageBuffer = &storage_addr;

            vkGetDescriptorEXT(ctx->core->device, &storage_desc, props.storageBufferDescriptorSize,
                ring + set0_offset + pipeline->set0_storage_offsets[i]);
        }

        if (!pipeline->uses_draw_uniforms) {
            buffer_indices[set_count] = 0;
            set_offsets[set_count] = set0_offset;
            ++set_count;
        }
    }

    if (pipeline->uses_single_texture) {
        VkDeviceSize tex_offset = 0;
        if (texture && texture->view != VK_NULL_HANDLE) {
            tex_offset = align_up(ring_offset, alignment);
            ring_offset = tex_offset + align_up(pipeline->texture_set_size, alignment);

            VkDescriptorImageInfo img_info{};
            img_info.sampler = texture_filter == TextureFilter::Nearest ? ctx->nearest_sampler : ctx->linear_sampler;
            img_info.imageView = texture->view;
            img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorGetInfoEXT tex_desc{};
            tex_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            tex_desc.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            tex_desc.data.pCombinedImageSampler = &img_info;

            vkGetDescriptorEXT(ctx->core->device, &tex_desc, props.combinedImageSamplerDescriptorSize,
                ring + tex_offset + pipeline->texture_binding_offset);
        }

        buffer_indices[set_count] = 0;
        set_offsets[set_count] = tex_offset;
        ++set_count;
    } else if (pipeline->uses_bindless_sampled_textures) {
        buffer_indices[set_count] = 1;
        set_offsets[set_count] = 0;
        ++set_count;
    }

    if (set_count > 0) {
        vkCmdSetDescriptorBufferOffsetsEXT(cmd->buffer, pipeline->bind_point,
            pipeline->layout, 0, set_count, buffer_indices, set_offsets);
    }
    return true;
}

void cmd_bind_uniform_texture(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, Handle<Buffer> uniform_h,
    Handle<Texture> texture_h, TextureFilter filter)
{
    NW_PROFILE_SCOPE_N("nw::gfx::cmd_bind_uniform_texture");

    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* pipeline = g_pipeline_pool.get(pipeline_h);
    auto* uniform = g_buffer_pool.get(uniform_h);
    if (!cmd || !pipeline || !uniform) {
        return;
    }

    VulkanCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = UniformSpan{
        .buffer = uniform_h,
        .offset = 0,
        .size = static_cast<uint32_t>(uniform->size),
        .data = nullptr,
    };
    next_resources.texture = texture_h;
    next_resources.texture_filter = filter;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    auto* tex = cmd->context->texture_pool_.get(texture_h);
    if (bind_pipeline_resources(cmd, pipeline, uniform, 0, static_cast<uint32_t>(uniform->size),
            {}, {}, {}, {}, {}, tex, filter, nullptr, 0, 0)) {
        cmd->bound_resources = next_resources;
        ++cmd->stats.resource_bind_count;
    }
}

void cmd_bind_uniform_texture(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    Handle<Texture> texture_h, TextureFilter filter)
{
    NW_PROFILE_SCOPE_N("nw::gfx::cmd_bind_uniform_texture(span)");

    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* pipeline = g_pipeline_pool.get(pipeline_h);
    auto* uniform = g_buffer_pool.get(uniforms.buffer);
    if (!cmd || !pipeline || !uniform) {
        return;
    }

    VulkanCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = uniforms;
    next_resources.texture = texture_h;
    next_resources.texture_filter = filter;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    auto* tex = cmd->context->texture_pool_.get(texture_h);
    if (bind_pipeline_resources(cmd, pipeline, uniform, uniforms.offset, uniforms.size,
            {}, {}, {}, {}, {}, tex, filter, nullptr, 0, 0)) {
        cmd->bound_resources = next_resources;
        ++cmd->stats.resource_bind_count;
    }
}

void cmd_bind_resources(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    StorageSpan storage0, StorageSpan storage1, const UniformSpan& uniforms2)
{
    cmd_bind_resources(cmd_ptr, pipeline_h, uniforms, storage0, storage1, {}, {}, {}, uniforms2);
}

void cmd_bind_resources(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    StorageSpan storage0, StorageSpan storage1, StorageSpan storage2, StorageSpan storage3, StorageSpan storage4,
    const UniformSpan& uniforms2)
{
    NW_PROFILE_SCOPE_N("nw::gfx::cmd_bind_resources");

    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* pipeline = g_pipeline_pool.get(pipeline_h);
    auto* uniform = g_buffer_pool.get(uniforms.buffer);
    auto* uniform2 = uniforms2.buffer.valid() ? g_buffer_pool.get(uniforms2.buffer) : nullptr;
    if (!cmd || !pipeline || !uniform) {
        return;
    }

    VulkanCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = uniforms;
    next_resources.storages[0] = storage0;
    next_resources.storages[1] = storage1;
    next_resources.storages[2] = storage2;
    next_resources.storages[3] = storage3;
    next_resources.storages[4] = storage4;
    next_resources.uniforms2 = uniforms2;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    if (bind_pipeline_resources(cmd, pipeline, uniform, uniforms.offset, uniforms.size,
            storage0, storage1, storage2, storage3, storage4, nullptr,
            TextureFilter::Linear, uniform2, uniforms2.offset, uniforms2.size)) {
        cmd->bound_resources = next_resources;
        ++cmd->stats.resource_bind_count;
    }
}

void cmd_bind_compute_resources(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    StorageSpan storage0, StorageSpan storage1, StorageSpan storage2, StorageSpan storage3, StorageSpan storage4)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* pipeline = g_pipeline_pool.get(pipeline_h);
    auto* uniform = uniforms.buffer.valid() ? g_buffer_pool.get(uniforms.buffer) : nullptr;
    if (!cmd || !pipeline) {
        return;
    }

    VulkanCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = uniforms;
    next_resources.storages[0] = storage0;
    next_resources.storages[1] = storage1;
    next_resources.storages[2] = storage2;
    next_resources.storages[3] = storage3;
    next_resources.storages[4] = storage4;
    next_resources.compute = true;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    if (bind_pipeline_resources(cmd, pipeline, uniform, uniforms.offset, uniforms.size,
            storage0, storage1, storage2, storage3, storage4, nullptr, TextureFilter::Linear, nullptr, 0, 0)) {
        cmd->bound_resources = next_resources;
        ++cmd->stats.resource_bind_count;
    }
}

void cmd_barrier_compute_to_graphics(CommandList* cmd_ptr)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT
        | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd->buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void cmd_draw(CommandList* cmd_ptr, uint32_t vertex_count, uint32_t instance_count)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    ++cmd->stats.draw_count;
    ++cmd->stats.nonindexed_draw_count;
    cmd->stats.draw_vertex_count += vertex_count;
    cmd->stats.draw_instance_count += instance_count;
    vkCmdDraw(cmd->buffer, vertex_count, instance_count, 0, 0);
}

void cmd_draw_indexed(CommandList* cmd_ptr, uint32_t index_count, uint32_t instance_count)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    ++cmd->stats.draw_count;
    ++cmd->stats.indexed_draw_count;
    cmd->stats.draw_index_count += index_count;
    cmd->stats.draw_instance_count += instance_count;
    vkCmdDrawIndexed(cmd->buffer, index_count, instance_count, 0, 0, 0);
}

void cmd_draw_indexed_base_instance(CommandList* cmd_ptr, uint32_t index_count, uint32_t first_instance,
    uint32_t instance_count)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    ++cmd->stats.draw_count;
    ++cmd->stats.indexed_draw_count;
    cmd->stats.draw_index_count += index_count;
    cmd->stats.draw_instance_count += instance_count;
    vkCmdDrawIndexed(cmd->buffer, index_count, instance_count, 0, 0, first_instance);
}

void cmd_draw_indexed_indirect(CommandList* cmd_ptr, IndirectDrawSpan commands, uint32_t draw_count,
    uint32_t stride, IndexedIndirectDrawStats stats)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* command_buffer = g_buffer_pool.get(commands.buffer);
    if (!cmd || !command_buffer || !command_buffer->buffer || draw_count == 0) {
        return;
    }
    if (stride < sizeof(IndexedIndirectDrawCommand)) {
        return;
    }

    const uint64_t required_size = sizeof(IndexedIndirectDrawCommand)
        + (static_cast<uint64_t>(draw_count) - 1u) * stride;
    if (commands.size != 0 && required_size > commands.size) {
        return;
    }
    if (static_cast<uint64_t>(commands.offset) + required_size > command_buffer->size) {
        return;
    }

    ++cmd->stats.indirect_draw_call_count;
    cmd->stats.draw_count += draw_count;
    cmd->stats.indexed_draw_count += draw_count;
    cmd->stats.draw_index_count += stats.index_count;
    cmd->stats.draw_instance_count += stats.instance_count;
    vkCmdDrawIndexedIndirect(cmd->buffer, command_buffer->buffer, commands.offset, draw_count, stride);
}

void cmd_draw_indexed_indirect_count(CommandList* cmd_ptr, IndirectDrawSpan commands, StorageSpan count_buffer,
    uint32_t max_draw_count, uint32_t stride)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    auto* command_buffer = g_buffer_pool.get(commands.buffer);
    auto* count = g_buffer_pool.get(count_buffer.buffer);
    if (!cmd || !command_buffer || !command_buffer->buffer || !count || !count->buffer || max_draw_count == 0) {
        return;
    }
    if (stride < sizeof(IndexedIndirectDrawCommand)) {
        return;
    }
    if (static_cast<uint64_t>(count_buffer.offset) + sizeof(uint32_t) > count->size) {
        return;
    }

    // The executed draw count is determined on the GPU; only the call itself can be counted here.
    ++cmd->stats.indirect_draw_call_count;
    vkCmdDrawIndexedIndirectCount(cmd->buffer, command_buffer->buffer, commands.offset,
        count->buffer, count_buffer.offset, max_draw_count, stride);
}

void cmd_dispatch(CommandList* cmd_ptr, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    ++cmd->stats.dispatch_count;
    vkCmdDispatch(cmd->buffer, group_count_x, group_count_y, group_count_z);
}

UniformSpan allocate_uniform_span(Context* ctx_ptr, uint32_t size, uint32_t alignment)
{
    auto* ctx = as_vulkan(ctx_ptr);
    if (!ctx || size == 0) {
        return {};
    }

    const VkDeviceSize min_alignment = std::max<VkDeviceSize>(
        ctx->core->properties.limits.minUniformBufferOffsetAlignment, 1);
    const VkDeviceSize actual_alignment = std::max<VkDeviceSize>(alignment, min_alignment);
    VkDeviceSize& offset = ctx->uniform_ring_offset[ctx->frame_index];
    const VkDeviceSize aligned_offset = align_up(offset, actual_alignment);
    if (aligned_offset + size > kUniformRingSize) {
        return {};
    }

    offset = aligned_offset + size;
    ++ctx->frame_stats.uniform_allocation_count;
    ctx->frame_stats.uniform_allocation_bytes += size;

    UniformSpan span{};
    span.buffer = ctx->uniform_ring_handle[ctx->frame_index];
    span.offset = static_cast<uint32_t>(aligned_offset);
    span.size = size;
    span.data = static_cast<uint8_t*>(ctx->uniform_ring_mapped[ctx->frame_index]) + aligned_offset;
    return span;
}

VertexSpan allocate_vertex_span(Context* ctx_ptr, uint32_t size, uint32_t alignment)
{
    auto* ctx = as_vulkan(ctx_ptr);
    if (!ctx || size == 0) {
        return {};
    }

    const VkDeviceSize actual_alignment = std::max<VkDeviceSize>(alignment, 1);
    VkDeviceSize& offset = ctx->vertex_ring_offset[ctx->frame_index];
    const VkDeviceSize aligned_offset = align_up(offset, actual_alignment);
    if (aligned_offset + size > kVertexRingSize) {
        return {};
    }

    offset = aligned_offset + size;
    ++ctx->frame_stats.vertex_allocation_count;
    ctx->frame_stats.vertex_allocation_bytes += size;

    VertexSpan span{};
    span.buffer = ctx->vertex_ring_handle[ctx->frame_index];
    span.offset = static_cast<uint32_t>(aligned_offset);
    span.size = size;
    span.data = static_cast<uint8_t*>(ctx->vertex_ring_mapped[ctx->frame_index]) + aligned_offset;
    return span;
}

BindlessTextureIndex get_bindless_texture_index(Context* ctx_ptr, Handle<Texture> texture)
{
    auto* ctx = as_vulkan(ctx_ptr);
    auto* tex = ctx ? ctx->texture_pool_.get(texture) : nullptr;
    return tex ? tex->bindless_slot : kInvalidBindlessTextureIndex;
}

Handle<RenderTarget> create_render_target(Context* ctx, const RenderTargetDesc& desc)
{
    auto* c = as_vulkan(ctx);
    VulkanRenderTarget rt{desc};
    return c->render_target_pool_.insert(std::move(rt));
}

void destroy_render_target(Context* ctx, Handle<RenderTarget> handle)
{
    auto* c = as_vulkan(ctx);
    auto* rt = c->render_target_pool_.get(handle);
    if (!rt) return;

    for (auto& att : rt->desc.color) {
        if (att.texture.valid()) destroy_texture(ctx, att.texture);
    }
    if (rt->desc.depth.texture.valid()) destroy_texture(ctx, rt->desc.depth.texture);

    c->render_target_pool_.destroy(handle);
}

void create_swapchain(Context* ctx_ptr, SDL_Window* window)
{
    auto* ctx = as_vulkan(ctx_ptr);
    auto* core = ctx->core;

    VkDevice device = core->device;
    VkPhysicalDevice physical_device = core->physical_device;

    if (ctx->surface == VK_NULL_HANDLE) {
        ctx->surface = create_surface(core, window);
    }

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, ctx->surface, &capabilities);

    // On Wayland, currentExtent is UINT32_MAX — query actual window size instead.
    uint32_t width, height;
    if (capabilities.currentExtent.width == UINT32_MAX) {
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
        width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    } else {
        width = capabilities.currentExtent.width;
        height = capabilities.currentExtent.height;
    }

    // Choose surface format and present mode
    VkSurfaceFormatKHR surface_format = choose_surface_format(physical_device, ctx->surface);
    VkPresentModeKHR present_mode = choose_present_mode(physical_device, ctx->surface);

    ctx->swapchain_format = surface_format.format;
    ctx->width = width;
    ctx->height = height;

    uint32_t image_count = MAX_SWAPCHAIN_IMAGES;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = {width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &ctx->swapchain));

    // Retrieve images
    uint32_t actual_image_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device, ctx->swapchain, &actual_image_count, nullptr));
    std::vector<VkImage> swapchain_images(actual_image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(device, ctx->swapchain, &actual_image_count, swapchain_images.data()));

    ctx->swapchain_image_count = actual_image_count;
    for (uint32_t i = 0; i < actual_image_count; ++i) {
        ctx->swapchain_images[i] = swapchain_images[i];

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surface_format.format,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};

        VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &ctx->swapchain_views[i]));
        ctx->swapchain_layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

void destroy_swapchain(VulkanContext* ctx)
{
    auto core = ctx->core;
    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        if (ctx->swapchain_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(core->device, ctx->swapchain_views[i], nullptr);
            ctx->swapchain_views[i] = VK_NULL_HANDLE;
        }
        ctx->swapchain_layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    if (ctx->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(core->device, ctx->swapchain, nullptr);
        ctx->swapchain = VK_NULL_HANDLE;
    }
}

Context* create_context(Core* core, const ContextDesc& desc)
{
    auto* ctx = new VulkanContext();
    ctx->core = as_vulkan(core);
    ctx->window = desc.window;
    ctx->width = desc.width;
    ctx->height = desc.height;
    ctx->headless = (desc.window == nullptr);
    ctx->frame_index = 0;
    ctx->frame_id = 0;
    ctx->gpu_timers_supported = ctx->core->graphics_queue_timestamp_valid_bits > 0
        && ctx->core->properties.limits.timestampPeriod > 0.0f;
    ctx->gpu_timestamp_period_ns = ctx->core->properties.limits.timestampPeriod;
    ctx->completed_gpu_timer_results.reserve(MAX_GPU_TIMERS_PER_FRAME);
    ctx->timestamp_query_results.reserve(MAX_GPU_TIMESTAMP_QUERIES_PER_FRAME * 2u);

    if (ctx->headless) {
        // Headless: build default render target from offscreen textures
        TextureDesc color_desc = {
            .width = desc.width,
            .height = desc.height,
            .format = Fmt::RGBA8,
            .sampled = true,
            .render_target = true,
        };

        TextureDesc depth_desc = {
            .width = desc.width,
            .height = desc.height,
            .format = Fmt::D32FS8,
            .render_target = true,
        };

        RenderTargetDesc rt_desc = {};
        rt_desc.color[0].texture = create_texture(as_gfx(ctx), color_desc);
        rt_desc.depth.texture = create_texture(as_gfx(ctx), depth_desc);

        ctx->default_render_target = create_render_target(as_gfx(ctx), rt_desc);
    } else {
        create_swapchain(as_gfx(ctx), desc.window);

        for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
            TextureDesc depth_desc = {
                .width = ctx->width,
                .height = ctx->height,
                .format = Fmt::D32FS8,
                .render_target = true,
            };

            RenderTargetDesc rt_desc = {};

            VulkanImage img{
                .image = VK_NULL_HANDLE, // Image
                .view = VK_NULL_HANDLE,
                .width = ctx->width,
                .height = ctx->height,
                .layers = 1,
                .mip_levels = 1,
                .owned = false,
            };

            rt_desc.color[0].texture = ctx->texture_pool_.insert(std::move(img));
            rt_desc.depth.texture = create_texture(as_gfx(ctx), depth_desc);

            ctx->swapchain_render_targets[i] = create_render_target(as_gfx(ctx), rt_desc);

            // Store depth view for quick access in cmd_begin_render
            auto* depth_tex = ctx->texture_pool_.get(rt_desc.depth.texture);
            if (depth_tex) {
                ctx->swapchain_depth_views[i] = depth_tex->view;
            }
        }
    }

    // Create per-frame sync primitives
    VkDevice device = ctx->core->device;
    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so first wait succeeds

    VkQueryPoolCreateInfo timestamp_query_info{};
    timestamp_query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    timestamp_query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    timestamp_query_info.queryCount = MAX_GPU_TIMESTAMP_QUERIES_PER_FRAME;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &sem_info, nullptr, &ctx->frames[i].image_acquired));
        VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &ctx->frames[i].in_flight));
        if (ctx->gpu_timers_supported) {
            VK_CHECK(vkCreateQueryPool(device, &timestamp_query_info, nullptr, &ctx->frames[i].timestamp_query_pool));
        }
    }
    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &sem_info, nullptr, &ctx->render_finished[i]));
    }

    ctx->bindless_texture_capacity = kBindlessTextureCapacity;
    ctx->bindless_texture_count = 1; // slot 0 is kInvalidBindlessTextureIndex.

    // Create per-frame descriptor ring buffers plus uniform arenas.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        GFX_CHECK(create_persistent_buffer(ctx, kDescriptorRingSize,
                      VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
                          | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT,
                      ctx->descriptor_ring[i], ctx->descriptor_ring_alloc[i],
                      ctx->descriptor_ring_mapped[i], ctx->descriptor_ring_addr[i]),
            "failed to create descriptor ring buffer");
        ctx->descriptor_ring_offset[i] = 0;

        BufferDesc uniform_desc{};
        uniform_desc.size = kUniformRingSize;
        uniform_desc.usage = BufferUsage::Uniform;
        uniform_desc.cpu_visible = true;
        ctx->uniform_ring_handle[i] = create_buffer(as_gfx(ctx), uniform_desc);
        auto* uniform = g_buffer_pool.get(ctx->uniform_ring_handle[i]);
        GFX_CHECK(uniform, "failed to create uniform ring buffer");
        ctx->uniform_ring[i] = uniform->buffer;
        ctx->uniform_ring_alloc[i] = uniform->allocation;
        ctx->uniform_ring_mapped[i] = uniform->mapped;
        ctx->uniform_ring_addr[i] = uniform->device_address;
        ctx->uniform_ring_offset[i] = 0;

        BufferDesc vertex_desc{};
        vertex_desc.size = kVertexRingSize;
        vertex_desc.usage = BufferUsage::Vertex;
        vertex_desc.cpu_visible = true;
        ctx->vertex_ring_handle[i] = create_buffer(as_gfx(ctx), vertex_desc);
        auto* vertex = g_buffer_pool.get(ctx->vertex_ring_handle[i]);
        GFX_CHECK(vertex, "failed to create vertex ring buffer");
        ctx->vertex_ring[i] = vertex->buffer;
        ctx->vertex_ring_alloc[i] = vertex->allocation;
        ctx->vertex_ring_mapped[i] = vertex->mapped;
        ctx->vertex_ring_addr[i] = vertex->device_address;
        ctx->vertex_ring_offset[i] = 0;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &ctx->linear_sampler));

    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &ctx->nearest_sampler));

    VkDescriptorSetLayoutBinding bindless_bindings[2]{};
    bindless_bindings[0].binding = 2;
    bindless_bindings[0].descriptorCount = ctx->bindless_texture_capacity;
    bindless_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindless_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindless_bindings[1].binding = 3;
    bindless_bindings[1].descriptorCount = 1;
    bindless_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindless_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo bindless_info{};
    bindless_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    bindless_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    bindless_info.bindingCount = 2;
    bindless_info.pBindings = bindless_bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &bindless_info, nullptr, &ctx->bindless_texture_layout));
    vkGetDescriptorSetLayoutSizeEXT(device, ctx->bindless_texture_layout, &ctx->bindless_texture_set_size);
    vkGetDescriptorSetLayoutBindingOffsetEXT(device, ctx->bindless_texture_layout, 2, &ctx->bindless_texture_binding_offset);
    vkGetDescriptorSetLayoutBindingOffsetEXT(device, ctx->bindless_texture_layout, 3, &ctx->bindless_sampler_binding_offset);

    GFX_CHECK(create_persistent_buffer(ctx,
                  ctx->bindless_texture_set_size,
                  VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT,
                  ctx->bindless_texture_table, ctx->bindless_texture_table_alloc,
                  ctx->bindless_texture_table_mapped, ctx->bindless_texture_table_addr),
        "failed to create bindless texture table");
    write_bindless_sampler_descriptor(ctx);

    return reinterpret_cast<Context*>(ctx);
}

void destroy_context(Context* ctx_ptr)
{
    auto* ctx = as_vulkan(ctx_ptr);

    vkDeviceWaitIdle(ctx->core->device);

    if (ctx->headless) {
        destroy_render_target(ctx_ptr, ctx->default_render_target);
    } else {
        for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
            destroy_render_target(ctx_ptr, ctx->swapchain_render_targets[i]);
        }

        destroy_swapchain(ctx);

        if (ctx->surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(ctx->core->instance, ctx->surface, nullptr);
            ctx->surface = VK_NULL_HANDLE;
        }
    }

    VkDevice device = ctx->core->device;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->frames[i].in_flight) vkDestroyFence(device, ctx->frames[i].in_flight, nullptr);
        if (ctx->frames[i].image_acquired) vkDestroySemaphore(device, ctx->frames[i].image_acquired, nullptr);
        if (ctx->frames[i].timestamp_query_pool) vkDestroyQueryPool(device, ctx->frames[i].timestamp_query_pool, nullptr);
    }
    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        if (ctx->render_finished[i]) vkDestroySemaphore(device, ctx->render_finished[i], nullptr);
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->frames[i].cmds.pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, ctx->frames[i].cmds.pool, nullptr);
            ctx->frames[i].cmds.pool = VK_NULL_HANDLE;
        }
    }
    if (ctx->linear_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, ctx->linear_sampler, nullptr);
    }
    if (ctx->nearest_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, ctx->nearest_sampler, nullptr);
    }
    if (ctx->bindless_texture_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, ctx->bindless_texture_layout, nullptr);
        ctx->bindless_texture_layout = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->uniform_ring_handle[i].valid()) {
            destroy_buffer(ctx->uniform_ring_handle[i]);
            ctx->uniform_ring_handle[i] = {};
        }
        if (ctx->vertex_ring_handle[i].valid()) {
            destroy_buffer(ctx->vertex_ring_handle[i]);
            ctx->vertex_ring_handle[i] = {};
        }
        destroy_persistent_buffer(ctx, ctx->descriptor_ring[i], ctx->descriptor_ring_alloc[i]);
    }
    destroy_persistent_buffer(ctx, ctx->bindless_texture_table, ctx->bindless_texture_table_alloc);

    delete ctx;
}

void wait_idle(Context* ctx_ptr)
{
    auto* ctx = as_vulkan(ctx_ptr);
    if (!ctx) {
        return;
    }
    vkDeviceWaitIdle(ctx->core->device);
}

bool get_frame_info(Context* ctx_ptr, FrameInfo& out) noexcept
{
    const auto* ctx = as_vulkan(ctx_ptr);
    if (!ctx) {
        out = FrameInfo{};
        return false;
    }

    out.frame_index = ctx->frame_index;
    out.frame_id = ctx->frame_id;
    out.width = ctx->width;
    out.height = ctx->height;
    out.headless = ctx->headless;
    out.drawable = ctx->headless || ctx->current_image_index < ctx->swapchain_image_count;
    return true;
}

bool get_command_stats(CommandList* cmd_ptr, CommandStats& out) noexcept
{
    const auto* cmd = reinterpret_cast<const VulkanCommandList*>(cmd_ptr);
    const auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx) {
        out = CommandStats{};
        return false;
    }

    out = cmd->stats;
    out.uniform_allocation_count += ctx->frame_stats.uniform_allocation_count;
    out.uniform_allocation_bytes += ctx->frame_stats.uniform_allocation_bytes;
    out.vertex_allocation_count += ctx->frame_stats.vertex_allocation_count;
    out.vertex_allocation_bytes += ctx->frame_stats.vertex_allocation_bytes;
    return true;
}

bool get_completed_gpu_timer_results(Context* ctx_ptr, std::vector<GpuTimerResult>& out) noexcept
{
    const auto* ctx = as_vulkan(ctx_ptr);
    if (!ctx || !ctx->gpu_timers_supported) {
        out.clear();
        return false;
    }

    out = ctx->completed_gpu_timer_results;
    return true;
}

bool resize_context(Context* ctx_ptr, uint32_t width, uint32_t height)
{
    auto* ctx = as_vulkan(ctx_ptr);
    if (!ctx) {
        return false;
    }

    width = std::max(1u, width);
    height = std::max(1u, height);

    if (ctx->width == width && ctx->height == height) {
        return true;
    }

    ctx->width = width;
    ctx->height = height;

    if (ctx->headless) {
        return true;
    }

    VkDevice device = ctx->core->device;
    vkDeviceWaitIdle(device);

    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        destroy_render_target(ctx_ptr, ctx->swapchain_render_targets[i]);
        ctx->swapchain_render_targets[i] = {};
        if (ctx->render_finished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, ctx->render_finished[i], nullptr);
            ctx->render_finished[i] = VK_NULL_HANDLE;
        }
    }

    destroy_swapchain(ctx);
    create_swapchain(ctx_ptr, ctx->window);

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        TextureDesc depth_desc = {
            .width = ctx->width,
            .height = ctx->height,
            .format = Fmt::D32FS8,
            .render_target = true,
        };

        RenderTargetDesc rt_desc = {};

        VulkanImage img{
            .image = VK_NULL_HANDLE,
            .view = VK_NULL_HANDLE,
            .width = ctx->width,
            .height = ctx->height,
            .layers = 1,
            .mip_levels = 1,
            .owned = false,
        };

        rt_desc.color[0].texture = ctx->texture_pool_.insert(std::move(img));
        rt_desc.depth.texture = create_texture(ctx_ptr, depth_desc);
        ctx->swapchain_render_targets[i] = create_render_target(ctx_ptr, rt_desc);

        // Update depth view for quick access
        auto* depth_tex = ctx->texture_pool_.get(rt_desc.depth.texture);
        if (depth_tex) {
            ctx->swapchain_depth_views[i] = depth_tex->view;
        }

        VK_CHECK(vkCreateSemaphore(device, &sem_info, nullptr, &ctx->render_finished[i]));
    }

    return true;
}

bool upload_texture_rgba8(Context* ctx_ptr, Handle<Texture> handle, const void* data, size_t size)
{
    NW_PROFILE_SCOPE_N("nw::gfx::upload_texture_rgba8");
    return upload_texture_pixels(ctx_ptr, handle, data, size, 4);
}

bool upload_texture_rgba16f(Context* ctx_ptr, Handle<Texture> handle, const void* data, size_t size)
{
    NW_PROFILE_SCOPE_N("nw::gfx::upload_texture_rgba16f");
    return upload_texture_pixels(ctx_ptr, handle, data, size, 8);
}

bool upload_texture_rgba16f_mips(Context* ctx_ptr, Handle<Texture> handle, const TextureMipData* levels, size_t count)
{
    NW_PROFILE_SCOPE_N("nw::gfx::upload_texture_rgba16f_mips");
    return upload_texture_pixels_mips(ctx_ptr, handle, levels, count, 8);
}

CommandList* begin_frame(Context* ctx_ptr)
{
    NW_PROFILE_SCOPE_N("nw::gfx::begin_frame");

    auto* ctx = as_vulkan(ctx_ptr);
    uint32_t frame_index = ctx->frame_index;
    PerFrame& frame = ctx->frames[frame_index];

    vkWaitForFences(ctx->core->device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->core->device, 1, &frame.in_flight);
    collect_completed_gpu_timer_results(ctx, frame);

    if (!ctx->headless) {
        uint32_t image_index = 0;
        VkResult res = vkAcquireNextImageKHR(
            ctx->core->device,
            ctx->swapchain,
            UINT64_MAX,
            frame.image_acquired,
            VK_NULL_HANDLE,
            &image_index);

        if (res != VK_SUCCESS)
            return nullptr;

        ctx->current_image_index = image_index;
    }

    // Reset descriptor ring offset — all descriptors from the previous use of this frame slot are now safe to overwrite.
    ctx->descriptor_ring_offset[frame_index] = 0;
    ctx->uniform_ring_offset[frame_index] = 0;
    ctx->vertex_ring_offset[frame_index] = 0;
    ctx->frame_stats = {};

    begin_command_buffer(ctx, frame_index);
    return reinterpret_cast<CommandList*>(&frame.cmds);
}

void end_frame(Context* ctx_ptr)
{
    NW_PROFILE_SCOPE_N("nw::gfx::end_frame");

    auto* ctx = as_vulkan(ctx_ptr);
    auto& frame = ctx->frames[ctx->frame_index];

    if (frame.cmds.in_render_pass) {
        vkCmdEndRendering(frame.cmds.buffer);
        frame.cmds.in_render_pass = false;
    }

    // Transition swapchain image COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR after rendering
    if (!ctx->headless && frame.cmds.rendered) {
        VkImageLayout old_layout = ctx->swapchain_layouts[ctx->current_image_index];
        VkAccessFlags src_access = 0;
        VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = old_layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = ctx->swapchain_images[ctx->current_image_index];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(frame.cmds.buffer,
            src_stage,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        ctx->swapchain_layouts[ctx->current_image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    // End recording
    vkEndCommandBuffer(frame.cmds.buffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore render_finished = ctx->render_finished[ctx->current_image_index];

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &frame.cmds.buffer;

    if (!ctx->headless) {
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &frame.image_acquired;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished;
    }

    VK_CHECK(vkQueueSubmit(ctx->core->graphics_queue, 1, &submit_info, frame.in_flight));

    if (!ctx->headless) {
        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &ctx->swapchain;
        present_info.pImageIndices = &ctx->current_image_index;

        vkQueuePresentKHR(ctx->core->graphics_queue, &present_info);
    }

    // Advance frame counters
    ctx->frame_index = (ctx->frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
    ctx->frame_id += 1;
}

bool capture_screenshot(Context* ctx_ptr, CommandList* cmd_ptr, const char* path)
{
    auto* ctx = as_vulkan(ctx_ptr);
    auto* cmd = reinterpret_cast<VulkanCommandList*>(cmd_ptr);
    if (!ctx || !cmd || !path) {
        return false;
    }

    const uint32_t width = ctx->width;
    const uint32_t height = ctx->height;
    const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * 4;

    VkBuffer readback_buffer = VK_NULL_HANDLE;
    VmaAllocation readback_alloc = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceAddress address = 0;
    if (!create_persistent_buffer(ctx, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            readback_buffer, readback_alloc, mapped, address)) {
        return false;
    }

    VkImage image = VK_NULL_HANDLE;
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ctx->headless) {
        auto* rt = ctx->render_target_pool_.get(ctx->default_render_target);
        auto* color_tex = rt ? ctx->texture_pool_.get(rt->desc.color[0].texture) : nullptr;
        if (!color_tex) {
            vmaDestroyBuffer(ctx->core->allocator, readback_buffer, readback_alloc);
            return false;
        }
        image = color_tex->image;
        current_layout = color_tex->layout;
    } else {
        if (ctx->current_image_index >= ctx->swapchain_image_count) {
            vmaDestroyBuffer(ctx->core->allocator, readback_buffer, readback_alloc);
            return false;
        }
        image = ctx->swapchain_images[ctx->current_image_index];
        current_layout = ctx->swapchain_layouts[ctx->current_image_index];
    }

    transition_image_layout(cmd->buffer, image,
        current_layout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd->buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer, 1, &region);

    transition_image_layout(cmd->buffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        current_layout,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    if (ctx->headless) {
        auto* rt = ctx->render_target_pool_.get(ctx->default_render_target);
        auto* color_tex = rt ? ctx->texture_pool_.get(rt->desc.color[0].texture) : nullptr;
        if (color_tex) {
            color_tex->layout = current_layout;
        }
    } else {
        ctx->swapchain_layouts[ctx->current_image_index] = current_layout;
    }

    end_frame(ctx_ptr);
    vkDeviceWaitIdle(ctx->core->device);

    const bool is_bgra = !ctx->headless
        && (ctx->swapchain_format == VK_FORMAT_B8G8R8A8_UNORM
            || ctx->swapchain_format == VK_FORMAT_B8G8R8A8_SRGB);
    std::vector<uint8_t> rgba(buffer_size);
    auto* src = static_cast<const uint8_t*>(mapped);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t src_idx = (static_cast<size_t>(y) * width + x) * 4;
            const size_t dst_idx = (static_cast<size_t>(y) * width + x) * 4;
            rgba[dst_idx + 0] = is_bgra ? src[src_idx + 2] : src[src_idx + 0];
            rgba[dst_idx + 1] = src[src_idx + 1];
            rgba[dst_idx + 2] = is_bgra ? src[src_idx + 0] : src[src_idx + 2];
            rgba[dst_idx + 3] = src[src_idx + 3];
        }
    }

    std::filesystem::path output_path{path};
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    const bool saved = stbi_write_png(path, static_cast<int>(width), static_cast<int>(height), 4,
                           rgba.data(), static_cast<int>(width * 4))
        != 0;

    vmaDestroyBuffer(ctx->core->allocator, readback_buffer, readback_alloc);
    return saved;
}

} // namespace nw::gfx
