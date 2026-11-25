#pragma once

#include "gfx.hpp"

#include <cstdint>

namespace nw::gfx {

// Pure-C++ escape hatch mirroring native_vulkan.hpp. Objective-C++ callers
// bridge these pointers via (__bridge id<MTL...>) without transferring
// ownership; lifetimes are owned by the corresponding nw::gfx objects.

struct NativeMetalCore {
    void* device = nullptr;        // id<MTLDevice>
    void* command_queue = nullptr; // id<MTLCommandQueue>
};

struct NativeMetalFrame {
    void* command_buffer = nullptr;         // id<MTLCommandBuffer>
    void* render_encoder = nullptr;         // id<MTLRenderCommandEncoder> (non-null during render pass)
    void* render_pass_descriptor = nullptr; // MTLRenderPassDescriptor* (valid for current render target)
    void* drawable = nullptr;               // id<CAMetalDrawable> (null when headless)
    void* drawable_texture = nullptr;       // id<MTLTexture>
    void* depth_stencil_texture = nullptr;  // id<MTLTexture> (swapchain/default depth-stencil)
    std::uint32_t pixel_format = 0;         // MTLPixelFormat
    std::uint32_t depth_pixel_format = 0;   // MTLPixelFormat
    std::uint32_t frame_index = 0;
    std::uint64_t frame_id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool headless = false;
};

struct NativeMetalBuffer {
    void* buffer = nullptr; // id<MTLBuffer>
    std::uint64_t size = 0;
    std::uint64_t gpu_address = 0;
};

struct NativeMetalTexture {
    void* texture = nullptr;        // id<MTLTexture>
    std::uint32_t pixel_format = 0; // MTLPixelFormat
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

bool get_native_metal_core(Core* core, NativeMetalCore& out) noexcept;
bool get_native_metal_frame(Context* ctx, CommandList* cmd, NativeMetalFrame& out) noexcept;
bool get_native_metal_buffer(Handle<Buffer> handle, NativeMetalBuffer& out) noexcept;
bool get_native_metal_texture(Context* ctx, Handle<Texture> handle, NativeMetalTexture& out) noexcept;

} // namespace nw::gfx
