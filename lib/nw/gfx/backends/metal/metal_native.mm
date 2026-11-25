#include "../../native_metal.hpp"
#include "metal_internal.hpp"

namespace nw::gfx {

bool get_native_metal_core(Core* core_ptr, NativeMetalCore& out) noexcept
{
    const auto* core = as_metal(core_ptr);
    if (!core || !core->device || !core->command_queue) {
        out = {};
        return false;
    }

    out.device = (__bridge void*)core->device;
    out.command_queue = (__bridge void*)core->command_queue;
    return true;
}

bool get_native_metal_frame(Context* ctx_ptr, CommandList* cmd_ptr, NativeMetalFrame& out) noexcept
{
    auto* ctx = as_metal(ctx_ptr);
    const auto* cmd = reinterpret_cast<const MetalCommandList*>(cmd_ptr);
    if (!ctx || !cmd || !cmd->command_buffer) {
        out = {};
        return false;
    }

    out.command_buffer = (__bridge void*)cmd->command_buffer;
    out.render_encoder = (__bridge void*)cmd->render_encoder;
    out.render_pass_descriptor = (__bridge void*)cmd->active_render_pass_descriptor;
    out.drawable = (__bridge void*)ctx->current_drawable;
    out.drawable_texture = ctx->current_drawable
        ? (__bridge void*)ctx->current_drawable.texture
        : nullptr;
    if (ctx->headless) {
        if (auto* rt = ctx->render_target_pool_.get(ctx->default_render_target)) {
            if (auto* ct = ctx->texture_pool_.get(rt->desc.color.texture)) {
                out.drawable_texture = (__bridge void*)ct->texture;
            }
            if (auto* dt = ctx->texture_pool_.get(rt->desc.depth.texture)) {
                out.depth_stencil_texture = (__bridge void*)dt->texture;
                out.depth_pixel_format = static_cast<std::uint32_t>(dt->format);
            }
        }
    } else if (auto* dt = ctx->texture_pool_.get(ctx->swapchain_depth_texture)) {
        out.depth_stencil_texture = (__bridge void*)dt->texture;
        out.depth_pixel_format = static_cast<std::uint32_t>(dt->format);
    }
    out.pixel_format = static_cast<std::uint32_t>(ctx->headless ? MTLPixelFormatRGBA8Unorm : ctx->swapchain_format);
    out.frame_index = ctx->frame_index;
    out.frame_id = ctx->frame_id;
    out.width = ctx->width;
    out.height = ctx->height;
    out.headless = ctx->headless;
    return true;
}

bool get_native_metal_buffer(Handle<Buffer> handle, NativeMetalBuffer& out) noexcept
{
    const auto* buffer = g_metal_buffer_pool.get(handle);
    if (!buffer || !buffer->buffer) {
        out = {};
        return false;
    }

    out.buffer = (__bridge void*)buffer->buffer;
    out.size = static_cast<std::uint64_t>(buffer->size);
    out.gpu_address = buffer->gpu_address;
    return true;
}

bool get_native_metal_texture(Context* ctx_ptr, Handle<Texture> handle, NativeMetalTexture& out) noexcept
{
    auto* ctx = as_metal(ctx_ptr);
    if (!ctx) {
        out = {};
        return false;
    }

    auto* tex = ctx->texture_pool_.get(handle);
    if (!tex || !tex->texture) {
        out = {};
        return false;
    }

    out.texture = (__bridge void*)tex->texture;
    out.pixel_format = static_cast<std::uint32_t>(tex->format);
    out.width = tex->width;
    out.height = tex->height;
    return true;
}

} // namespace nw::gfx
