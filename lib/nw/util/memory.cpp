#include "memory.hpp"

#include "../log.hpp"

namespace nw {

// == MemoryArena ==============================================================
// =============================================================================

inline void* align_ptr(void* ptr, size_t alignment)
{
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    size_t offset = (alignment - (p % alignment)) % alignment;
    return reinterpret_cast<void*>(p + offset);
}

MemoryArena::MemoryArena(size_t size)
    : size_(size)
{
    blocks_.reserve(8);
}

MemoryArena::~MemoryArena()
{
    for (auto& block : blocks_) {
        free(block.block);
    }
}

void* MemoryArena::allocate(std::size_t size, std::size_t alignment)
{
    size_t aligned_pos = reinterpret_cast<size_t>(align_ptr(blocks_[current_block_].block + blocks_[current_block_].position, alignment));
    size_t padding = aligned_pos - (reinterpret_cast<size_t>(blocks_[current_block_].block + blocks_[current_block_].position));

    if (blocks_[current_block_].position + padding + size > blocks_[current_block_].size) {
        alloc_block_(std::max(size + alignment, size_));
        aligned_pos = reinterpret_cast<size_t>(align_ptr(blocks_[current_block_].block + blocks_[current_block_].position, alignment));
        padding = aligned_pos - reinterpret_cast<size_t>(blocks_[current_block_].block + blocks_[current_block_].position);
    }

    void* aligned_mem = blocks_[current_block_].block + blocks_[current_block_].position + padding;
    blocks_[current_block_].position += padding + size;
    return aligned_mem;
}

MemoryMarker MemoryArena::current()
{
    MemoryMarker result;
    result.chunk_index = current_block_;
    result.position = blocks_[current_block_].position;
    return result;
}

void MemoryArena::reset()
{
    current_block_ = 0;
    blocks_[current_block_].position = 0;
}

void MemoryArena::rewind(MemoryMarker marker)
{
    current_block_ = marker.chunk_index;
    blocks_[current_block_].position = marker.position;
}

void MemoryArena::alloc_block_(size_t size)
{
    if (current_block_ + 1 < blocks_.size()) {
        ++current_block_;
        if (blocks_[current_block_].size < size) {
            blocks_[current_block_].block = static_cast<uint8_t*>(realloc(blocks_[current_block_].block, size));
            blocks_[current_block_].size = size;
        }
    } else {
        MemoryBlock mb;
        mb.block = static_cast<uint8_t*>(malloc(size));
        mb.size = size;
        blocks_.push_back(mb);
        current_block_ = blocks_.size() - 1;
    }

    CHECK_F(blocks_[current_block_].size >= size, "Failed to allocate a block of size '{}', only got '{}'", size,
        blocks_[current_block_].size >= size);
}

void* MemoryArena::do_allocate(size_t size, size_t alignment)
{
    return allocate(size, alignment);
}

void MemoryArena::do_deallocate(void*, size_t, size_t)
{
}

bool MemoryArena::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

} // namespace nw
