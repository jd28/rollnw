#include "memory.hpp"

#include "../log.hpp"

#include <algorithm>

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
    if (blocks_.size() == 0) { alloc_block_(std::max(size + alignment, size_)); }

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
    if (blocks_.size() == 0) { alloc_block_(size_); }

    MemoryMarker result;
    result.chunk_index = current_block_;
    result.position = blocks_[current_block_].position;
    return result;
}

void MemoryArena::reset()
{
    if (blocks_.size() == 0) { return; }
    current_block_ = 0;
    blocks_[current_block_].position = 0;
}

void MemoryArena::rewind(MemoryMarker marker)
{
    CHECK_F(marker.chunk_index < blocks_.size(), "Memory marker mismatched");
    current_block_ = marker.chunk_index;
    blocks_[current_block_].position = marker.position;
}

void MemoryArena::alloc_block_(size_t size)
{
    if (current_block_ + 1 < blocks_.size()) {
        ++current_block_;
        blocks_[current_block_].position = 0;
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

// == MemoryScope ==============================================================
// =============================================================================

MemoryScope::MemoryScope(MemoryArena* arena)
    : arena_{arena}
    , marker_{arena->current()}
{
}

MemoryScope::MemoryScope(MemoryScope&& other)
{
    this->arena_ = other.arena_;
    this->finalizers_ = other.finalizers_;
    this->marker_ = other.marker_;

    other.arena_ = nullptr;
    other.finalizers_ = nullptr;
    other.marker_ = MemoryMarker{};
}

MemoryScope& MemoryScope::operator=(MemoryScope&& other)
{
    if (this != &other) {
        this->arena_ = other.arena_;
        this->finalizers_ = other.finalizers_;
        this->marker_ = other.marker_;

        other.arena_ = nullptr;
        other.finalizers_ = nullptr;
        other.marker_ = MemoryMarker{};
    }
    return *this;
}

MemoryScope::~MemoryScope()
{
    auto f = finalizers_;
    while (f) {
        auto obj = reinterpret_cast<uint8_t*>(f) + sizeof(detail::Finalizer);
        f->fn(obj);
        f = f->next;
    }
    arena_->rewind(marker_);
}

void* MemoryScope::alloc(size_t size, size_t alignment)
{
    return arena_->allocate(size, alignment);
}

// == MemoryPool ========================================================================
// ======================================================================================

namespace detail {

PoolBlock::PoolBlock(std::size_t block_size, std::size_t block_count)
    : block_size_(block_size)
    , block_count_(block_count)
{
    expand(block_count_);
}

PoolBlock::~PoolBlock()
{
    for (auto block : blocks_) {
        free(block);
    }
}

std::size_t PoolBlock::block_size() const noexcept
{
    return block_size_;
}

void* PoolBlock::allocate()
{
    if (free_list_.empty()) {
        expand(1);
    }
    void* ptr = free_list_.back();
    free_list_.pop_back();
    return ptr;
}

void PoolBlock::deallocate(void* ptr)
{
    free_list_.push_back(ptr);
}

void PoolBlock::expand(std::size_t count)
{
    std::size_t total = block_size_ * count;
    void* block = malloc(total);
    blocks_.push_back(block);
    for (std::size_t i = 0; i < count; ++i) {
        free_list_.push_back(static_cast<char*>(block) + i * block_size_);
    }
}

} // namespace detail

inline size_t next_power_of_2(size_t size)
{
    if (size == 0) return 1;
    return std::pow(2, std::ceil(std::log2(size)));
}

MemoryPool::MemoryPool(size_t max_size, size_t count)
    : max_size_(std::max(max_size, size_t(128)))
    , count_(count)
{
    // Make the smaller buckets more granular to avoid wasting too much memory
    for (size_t size = 8; size <= 128; size += 8) {
        pools_.emplace_back(size, count);
    }

    for (size_t size = 256; size <= max_size_; size *= 2) {
        pools_.emplace_back(size, count);
    }
}

void* MemoryPool::allocate(size_t size)
{
    for (auto& pool : pools_) {
        if (size <= pool.block_size()) {
            return pool.allocate();
        }
    }
    return malloc(size);
}

void MemoryPool::deallocate(void* ptr, size_t size)
{
    for (auto& pool : pools_) {
        if (size <= pool.block_size()) {
            pool.deallocate(ptr);
            return;
        }
    }
    free(ptr);
}

} // namespace nw
