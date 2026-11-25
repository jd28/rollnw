#import <Metal/Metal.h>

#include <backends/imgui_impl_metal.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <nw/gfx/native_metal.hpp>

namespace mudl {

bool imgui_metal_init(nw::gfx::Core* core, SDL_Window* window)
{
    nw::gfx::NativeMetalCore metal_core{};
    if (!nw::gfx::get_native_metal_core(core, metal_core) || !metal_core.device) {
        return false;
    }
    if (!ImGui_ImplSDL3_InitForMetal(window)) {
        return false;
    }
    id<MTLDevice> device = (__bridge id<MTLDevice>)metal_core.device;
    if (!ImGui_ImplMetal_Init(device)) {
        ImGui_ImplSDL3_Shutdown();
        return false;
    }
    return true;
}

void imgui_metal_shutdown()
{
    ImGui_ImplMetal_Shutdown();
}

void imgui_metal_new_frame(nw::gfx::Context* ctx, nw::gfx::CommandList* cmd)
{
    @autoreleasepool {
        nw::gfx::NativeMetalFrame frame{};
        if (!nw::gfx::get_native_metal_frame(ctx, cmd, frame)) {
            return;
        }
        MTLRenderPassDescriptor* rpd = (__bridge MTLRenderPassDescriptor*)frame.render_pass_descriptor;
        if (!rpd) {
            rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            if (frame.drawable_texture) {
                rpd.colorAttachments[0].texture = (__bridge id<MTLTexture>)frame.drawable_texture;
            }
            if (frame.depth_stencil_texture) {
                id<MTLTexture> ds = (__bridge id<MTLTexture>)frame.depth_stencil_texture;
                rpd.depthAttachment.texture = ds;
                if (ds.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
                    rpd.stencilAttachment.texture = ds;
                }
            }
        }
        ImGui_ImplMetal_NewFrame(rpd);
    }
}

void imgui_metal_render_draw_data(nw::gfx::Context* ctx, nw::gfx::CommandList* cmd, ImDrawData* draw_data)
{
    @autoreleasepool {
        if (!draw_data) {
            return;
        }
        nw::gfx::NativeMetalFrame frame{};
        if (!nw::gfx::get_native_metal_frame(ctx, cmd, frame)
            || !frame.command_buffer
            || !frame.render_encoder) {
            return;
        }
        id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)frame.command_buffer;
        id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)frame.render_encoder;
        ImGui_ImplMetal_RenderDrawData(draw_data, cb, enc);
    }
}

} // namespace mudl
