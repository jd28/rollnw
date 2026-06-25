#pragma once

#include <nw/render/model_gpu_backend.hpp>

namespace nw::gfx {
struct Context;
}

namespace nw::render {

// Shared model renderer backend context. This is the common GPU submission
// contract for modern RenderModel/PBR draws; legacy asset cache ownership stays
// in the NWN sidecar context.
struct ModelRenderContext {
    nw::gfx::Context* gfx = nullptr;
    ModelGpuBackend* gpu = nullptr;
};

} // namespace nw::render
