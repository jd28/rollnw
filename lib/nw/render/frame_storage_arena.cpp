#include "frame_storage_arena.hpp"

#include <nw/log.hpp>

#include <algorithm>
#include <cstring>

namespace nw::render {

namespace {

constexpr size_t kInitialSize = 4 * 1024 * 1024;

size_t align_to(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

FrameStorageArena::~FrameStorageArena()
{
    destroy();
}

void FrameStorageArena::destroy()
{
    for (auto& block : blocks_) {
        if (block.buffer.valid()) nw::gfx::destroy_buffer(block.buffer);
    }
    blocks_.clear();
    frame_id_ = UINT64_MAX;
}

bool FrameStorageArena::reset(
    nw::gfx::Context* ctx, uint64_t new_frame_id, size_t min_capacity, nw::gfx::BufferUsage usage)
{
    if (usage_ != usage) {
        destroy();
    }
    usage_ = usage;

    if (!blocks_.empty()) {
        const auto largest = std::max_element(blocks_.begin(), blocks_.end(),
            [](const Block& lhs, const Block& rhs) {
                return lhs.capacity < rhs.capacity;
            });
        Block retained = *largest;
        retained.offset = 0;
        for (auto& block : blocks_) {
            if (block.buffer != retained.buffer && block.buffer.valid()) {
                nw::gfx::destroy_buffer(block.buffer);
            }
        }
        blocks_.clear();
        blocks_.push_back(retained);
    }

    const size_t old_cap = blocks_.empty() ? 0 : blocks_.back().capacity;
    if (old_cap < min_capacity) {
        const size_t new_cap = std::max(min_capacity, std::max(kInitialSize, old_cap * 2));
        nw::gfx::BufferDesc desc{};
        desc.size = new_cap;
        desc.usage = usage_;
        desc.cpu_visible = true;
        auto buffer = nw::gfx::create_buffer(ctx, desc);
        if (!buffer.valid()) {
            LOG_F(ERROR, "Failed to create frame storage arena block");
            frame_id_ = UINT64_MAX;
            return false;
        }
        void* mapped = nw::gfx::map_buffer(buffer);
        if (!mapped) {
            nw::gfx::destroy_buffer(buffer);
            LOG_F(ERROR, "Failed to map frame storage arena block");
            frame_id_ = UINT64_MAX;
            return false;
        }
        blocks_.push_back({buffer, mapped, new_cap, 0});
    }
    frame_id_ = new_frame_id;
    return true;
}

nw::gfx::StorageSpan FrameStorageArena::allocate(nw::gfx::Context* ctx, uint32_t size, uint32_t alignment)
{
    if (blocks_.empty()) {
        return {};
    }

    auto& back = blocks_.back();
    const size_t offset = align_to(back.offset, alignment);
    if (offset + size <= back.capacity) {
        back.offset = offset + size;
        return {back.buffer, static_cast<uint32_t>(offset), size};
    }

    const size_t new_cap = std::max<size_t>(size, back.capacity * 2);
    nw::gfx::BufferDesc desc{};
    desc.size = new_cap;
    desc.usage = usage_;
    desc.cpu_visible = true;
    auto buffer = nw::gfx::create_buffer(ctx, desc);
    if (!buffer.valid()) {
        LOG_F(ERROR, "Frame storage arena overflow: need {} bytes", size);
        return {};
    }
    void* mapped = nw::gfx::map_buffer(buffer);
    if (!mapped) {
        nw::gfx::destroy_buffer(buffer);
        return {};
    }
    blocks_.push_back({buffer, mapped, new_cap, size});
    return {buffer, 0, size};
}

nw::gfx::StorageSpan FrameStorageArena::write(
    nw::gfx::Context* ctx, const void* data, uint32_t size, uint32_t alignment)
{
    auto span = allocate(ctx, size, alignment);
    if (!span.buffer.valid()) return {};
    std::memcpy(static_cast<uint8_t*>(blocks_.back().mapped) + span.offset, data, size);
    return span;
}

MappedStorageSpan FrameStorageArena::allocate_mapped(nw::gfx::Context* ctx, uint32_t size, uint32_t alignment)
{
    auto span = allocate(ctx, size, alignment);
    if (!span.buffer.valid() || blocks_.empty()) return {};
    return {span, static_cast<uint8_t*>(blocks_.back().mapped) + span.offset};
}

} // namespace nw::render
