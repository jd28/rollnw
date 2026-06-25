#pragma once

#include <nw/render/model_gpu_backend.hpp>

namespace nw::render::nwn {

class ModelGpuResources {
public:
    explicit ModelGpuResources(nw::gfx::Context* ctx)
        : ctx_{ctx}
    {
    }

    ~ModelGpuResources();

    [[nodiscard]] bool initialize();
    void destroy();

private:
    nw::gfx::Context* ctx_ = nullptr;
};

// Compatibility aliases for legacy renderer code while the shared backend type
// and pipeline keys move into the common renderer namespace.
using MappedIndirectDrawSpan = nw::render::MappedIndirectDrawSpan;
using ModelGpuBackend = nw::render::ModelGpuBackend;
using ModelPipelineKey = nw::render::ModelPipelineKey;
using ModelPipelineMeshKind = nw::render::ModelPipelineMeshKind;
using ModelPipelinePass = nw::render::ModelPipelinePass;
using ModelPipelineTarget = nw::render::ModelPipelineTarget;

} // namespace nw::render::nwn
