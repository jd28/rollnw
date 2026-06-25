#include "pipeline_cache.hpp"

namespace nw::render {
namespace {

static_assert(sizeof(nw::gfx::PipelineDesc) == 96,
    "PipelineDesc layout changed; verify PipelineDesc equality/cache-key coverage");

bool same_pipeline_desc(const nw::gfx::PipelineDesc& lhs, const nw::gfx::PipelineDesc& rhs)
{
    return lhs == rhs;
}

} // namespace

PipelineCache::PipelineCache(nw::gfx::Context* ctx) noexcept
    : ctx_{ctx}
{
}

PipelineCache::~PipelineCache()
{
    destroy();
}

void PipelineCache::reset(nw::gfx::Context* ctx)
{
    destroy();
    ctx_ = ctx;
}

nw::gfx::Handle<nw::gfx::Pipeline> PipelineCache::get_or_create(const nw::gfx::PipelineDesc& desc)
{
    for (const auto& entry : entries_) {
        if (same_pipeline_desc(entry.desc, desc)) {
            return entry.pipeline;
        }
    }

    if (!ctx_) {
        return {};
    }

    auto pipeline = nw::gfx::create_pipeline(ctx_, desc);
    if (pipeline.valid()) {
        entries_.push_back(Entry{
            .desc = desc,
            .pipeline = pipeline,
        });
    }
    return pipeline;
}

void PipelineCache::destroy()
{
    if (ctx_) {
        for (const auto& entry : entries_) {
            if (entry.pipeline.valid()) {
                nw::gfx::destroy_pipeline(ctx_, entry.pipeline);
            }
        }
    }
    entries_.clear();
}

} // namespace nw::render
