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
};

} // namespace nw::render
