#pragma once

#include <nw/gfx/gfx.hpp>

#include <cstddef>
#include <vector>

namespace nw::render {

class PipelineCache {
public:
    explicit PipelineCache(nw::gfx::Context* ctx = nullptr) noexcept;
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&&) = delete;
    PipelineCache& operator=(PipelineCache&&) = delete;
    ~PipelineCache();

    void reset(nw::gfx::Context* ctx);
    [[nodiscard]] nw::gfx::Handle<nw::gfx::Pipeline> get_or_create(const nw::gfx::PipelineDesc& desc);
    void destroy();

    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        nw::gfx::PipelineDesc desc;
        nw::gfx::Handle<nw::gfx::Pipeline> pipeline;
    };

    nw::gfx::Context* ctx_ = nullptr;
    std::vector<Entry> entries_;
};

} // namespace nw::render
