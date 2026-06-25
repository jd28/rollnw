#pragma once

#include <nw/render/model_render_context.hpp>

namespace nw::render::nwn {

class ModelGpuResources;
class RenderAssetCache;

// Legacy NWN model submission needs the common GPU context plus the sidecar
// GPU resources and asset cache for PLT/source texture lookup. New
// RenderModel/PBR call sites should request nw::render::ModelRenderContext
// directly.
struct ModelRenderContext {
    nw::gfx::Context* gfx = nullptr;
    nw::render::ModelGpuBackend* gpu = nullptr;
    RenderAssetCache* assets = nullptr;
    ModelGpuResources* legacy_gpu = nullptr;
};

} // namespace nw::render::nwn
