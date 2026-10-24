#pragma once

#include <nw/render/model_gpu_backend.hpp>

namespace nw::gfx {
struct Context;
}

namespace nw::render {

// Shared GPU submission context for RenderModel/PBR draws.
struct ModelRenderContext {
    nw::gfx::Context* gfx = nullptr;
    ModelGpuBackend* gpu = nullptr;
    // Optional shared b1 constants for one immutable view in the current frame.
    // Rebuild after view/light/shadow changes; never retain across frames/devices.
    // An empty span makes standalone submission prepare its own batch view.
    nw::gfx::UniformSpan view_uniforms{};
};

} // namespace nw::render
