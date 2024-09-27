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
    , block_(nullptr)
    , pos_(0)
    , end_(0)
{
    blocks_.reserve(8);
    alloc_block_(size);
}

MemoryArena::~MemoryArena()
{
    for (void* block : blocks_) {
        free(block);
    }
}

void* MemoryArena::allocate(std::size_t size, std::size_t alignment)
{
    size_t aligned_pos = reinterpret_cast<size_t>(align_ptr(block_ + pos_, alignment));
    size_t padding = aligned_pos - (reinterpret_cast<size_t>(block_ + pos_));

    if (pos_ + padding + size > end_) {
        alloc_block_(std::max(size + alignment, size_));
        aligned_pos = reinterpret_cast<size_t>(align_ptr(block_ + pos_, alignment));
        padding = aligned_pos - reinterpret_cast<size_t>(block_ + pos_);
    }

    void* aligned_mem = block_ + pos_ + padding;
    pos_ += padding + size;
    return aligned_mem;
}

/// Reset the arena, freeing all blocks except the first.
void MemoryArena::reset()
{
    for (size_t i = 1; i < blocks_.size(); ++i) {
        std::free(blocks_[i]);
    }
    blocks_.resize(1); // Keep only the first block.
    block_ = static_cast<uint8_t*>(blocks_[0]);
    pos_ = 0;
    end_ = size_;
}

void MemoryArena::alloc_block_(size_t size)
{
    block_ = static_cast<uint8_t*>(malloc(size));
    blocks_.push_back(block_);
    pos_ = 0;
    end_ = size;
}

} // namespace nw
