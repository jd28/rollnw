#pragma once

#include <nw/gfx/gfx.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nw::render {

/// A storage span plus a CPU pointer to its mapped backing memory, for callers
/// that write the allocation in place (e.g. indirect draw commands, static draw data).
struct MappedStorageSpan {
    nw::gfx::StorageSpan span;
    void* data = nullptr;
};

/// Per-frame bump allocator over CPU-visible GPU buffers. Double-buffer one arena
/// per frame-in-flight and `reset()` it once per frame; allocations made during the
/// frame are reclaimed on the next `reset()` with the same `frame_id`. `usage`
/// selects the buffer usage (Storage by default; Indirect for draw-command buffers).
class FrameStorageArena {
public:
    ~FrameStorageArena();

    [[nodiscard]] nw::gfx::StorageSpan write(
        nw::gfx::Context* ctx, const void* data, uint32_t size, uint32_t alignment = 64);
    [[nodiscard]] MappedStorageSpan allocate_mapped(
        nw::gfx::Context* ctx, uint32_t size, uint32_t alignment = 64);
    [[nodiscard]] bool reset(nw::gfx::Context* ctx, uint64_t new_frame_id, size_t min_capacity = 0,
        nw::gfx::BufferUsage usage = nw::gfx::BufferUsage::Storage);
    void destroy();

    [[nodiscard]] uint64_t frame_id() const noexcept { return frame_id_; }
    [[nodiscard]] size_t debug_block_count() const noexcept { return blocks_.size(); }
    [[nodiscard]] size_t debug_retained_capacity() const noexcept
    {
        return blocks_.empty() ? 0 : blocks_.back().capacity;
    }

private:
    struct Block {
        nw::gfx::Handle<nw::gfx::Buffer> buffer;
        void* mapped = nullptr;
        size_t capacity = 0;
        size_t offset = 0;
    };

    [[nodiscard]] nw::gfx::StorageSpan allocate(nw::gfx::Context* ctx, uint32_t size, uint32_t alignment);

    std::vector<Block> blocks_;
    nw::gfx::BufferUsage usage_ = nw::gfx::BufferUsage::Storage;
    uint64_t frame_id_ = UINT64_MAX;
};

} // namespace nw::render
