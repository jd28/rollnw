#include "memory.hpp"

#include "../kernel/Memory.hpp"
#include "../log.hpp"
#include "asan.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace nw {

namespace detail {
struct alignas(std::max_align_t) MemoryHeader {
    void* original_ptr;
    std::size_t size;
};

bool checked_allocation_size(size_t size, size_t alignment, size_t& total_size) noexcept
{
    alignment = std::max(alignment, alignof(MemoryHeader));
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) { return false; }
    if (size > std::numeric_limits<size_t>::max() - sizeof(MemoryHeader)) { return false; }

    const size_t with_header = size + sizeof(MemoryHeader);
    if (with_header > std::numeric_limits<size_t>::max() - (alignment - 1)) { return false; }

    total_size = with_header + alignment - 1;
    return true;
}

uintptr_t align_up(uintptr_t address, size_t alignment) noexcept
{
    return (address + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
}
} // namespace detail

void* GlobalMemory::allocate(size_t size, size_t alignment)
{
    alignment = std::max(alignment, alignof(detail::MemoryHeader));
    size_t total_size = 0;
    if (!detail::checked_allocation_size(size, alignment, total_size)) { return nullptr; }

    void* original = malloc(total_size);
    if (!original) { return nullptr; }

    uintptr_t raw_address = reinterpret_cast<uintptr_t>(original) + sizeof(detail::MemoryHeader);
    uintptr_t aligned_address = detail::align_up(raw_address, alignment);

    detail::MemoryHeader* header = reinterpret_cast<detail::MemoryHeader*>(aligned_address - sizeof(detail::MemoryHeader));
    header->original_ptr = original;
    header->size = total_size;

    return reinterpret_cast<void*>(aligned_address);
}

void GlobalMemory::deallocate(void* ptr)
{
    if (!ptr) return;
    detail::MemoryHeader* header = reinterpret_cast<detail::MemoryHeader*>(static_cast<char*>(ptr) - sizeof(detail::MemoryHeader));
    free(header->original_ptr);
}

// == MemoryArena ==============================================================
// =============================================================================

MemoryArena::MemoryArena(size_t capacity)
    : default_block_size_(capacity)
{
    allocate_block(capacity);
}

MemoryArena::~MemoryArena()
{
    deallocate_blocks();
}

void* MemoryArena::allocate(std::size_t size, std::size_t alignment)
{
    CHECK_F((alignment & (alignment - 1)) == 0, "Alignment must be a power of two");

    auto& block = blocks_[current_block_];
    uintptr_t current = reinterpret_cast<uintptr_t>(block.base) + block.used;
    uintptr_t aligned = (current + (alignment - 1)) & ~(alignment - 1);
    size_t padding = aligned - current;
    size_t needed = padding + size;

    if (block.used <= block.capacity && needed <= block.capacity - block.used) {
        block.used += needed;
        nw::asan::unpoison(reinterpret_cast<void*>(aligned), size);
        return reinterpret_cast<void*>(aligned);
    }

    // Current block is full, allocate a new one
    size_t new_capacity = std::max(default_block_size_, size + alignment);
    allocate_block(new_capacity);

    auto& new_block = blocks_[current_block_];
    current = reinterpret_cast<uintptr_t>(new_block.base);
    aligned = (current + (alignment - 1)) & ~(alignment - 1);
    padding = aligned - current;
    needed = padding + size;

    new_block.used += needed;
    nw::asan::unpoison(reinterpret_cast<void*>(aligned), size);
    return reinterpret_cast<void*>(aligned);
}

size_t MemoryArena::capacity() const noexcept
{
    size_t total = 0;
    for (const auto& block : blocks_) {
        total += block.capacity;
    }
    return total;
}

MemoryMarker MemoryArena::current()
{
    return {current_block_, blocks_[current_block_].used};
}

void MemoryArena::reset()
{
    if (blocks_.empty()) { return; }
    for (size_t i = 1; i < blocks_.size(); ++i) {
        free(blocks_[i].base);
    }
    blocks_.resize(1);
    current_block_ = 0;
    blocks_[0].used = 0;
    // Everything handed out is now dead; poison the retained block so any
    // surviving pointer into arena memory trips ASan.
    nw::asan::poison(blocks_[0].base, blocks_[0].capacity);
}

void MemoryArena::rewind(MemoryMarker marker)
{
    current_block_ = marker.block_index;
    blocks_[current_block_].used = marker.position;
    // Poison everything above the rewind point in the target block, plus every
    // block past it — those allocations no longer exist.
    auto& block = blocks_[current_block_];
    nw::asan::poison(static_cast<uint8_t*>(block.base) + marker.position,
        block.capacity - marker.position);
    for (size_t i = current_block_ + 1; i < blocks_.size(); ++i) {
        nw::asan::poison(blocks_[i].base, blocks_[i].capacity);
    }
}

size_t MemoryArena::used() const noexcept
{
    size_t total = 0;
    for (const auto& block : blocks_) {
        total += block.used;
    }
    return total;
}

void MemoryArena::allocate_block(size_t capacity)
{
    void* base = malloc(capacity);
    CHECK_F(!!base, "Unable to allocate block of size: {}", capacity);
    current_block_ = blocks_.size();
    blocks_.push_back({base, 0, capacity});
    // Whole block starts dead; allocate() unpoisons each sub-range it hands out.
    nw::asan::poison(base, capacity);
}

void MemoryArena::deallocate_blocks()
{
    for (auto& block : blocks_) {
        free(block.base);
    }
    blocks_.clear();
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
    arena_ = other.arena_;
    finalizers_ = other.finalizers_;
    marker_ = other.marker_;

    other.arena_ = nullptr;
    other.finalizers_ = nullptr;
    other.marker_ = MemoryMarker{};
}

MemoryScope& MemoryScope::operator=(MemoryScope&& other)
{
    if (this != &other) {
        reset();
        arena_ = other.arena_;
        finalizers_ = other.finalizers_;
        marker_ = other.marker_;

        other.arena_ = nullptr;
        other.finalizers_ = nullptr;
        other.marker_ = MemoryMarker{};
    }
    return *this;
}

MemoryScope::~MemoryScope()
{
    reset();
}

void* MemoryScope::allocate(size_t size, size_t alignment)
{
    if (!arena_) {
        throw std::bad_alloc();
    }
    void* mem = arena_->allocate(size, alignment);
    if (!mem) {
        throw std::bad_alloc();
    }
    return mem;
}

void MemoryScope::reset()
{
    if (arena_) {
        auto f = finalizers_;
        while (f) {
            auto next = f->next;
            f->fn(f->object);
            f = next;
        }
        arena_->rewind(marker_);
    }
    finalizers_ = nullptr;
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
    nw::asan::unpoison(ptr, block_size_);
    return ptr;
}

void PoolBlock::deallocate(void* ptr)
{
    nw::asan::poison(ptr, block_size_);
    free_list_.push_back(ptr);
}

void PoolBlock::expand(std::size_t count)
{
    std::size_t total = block_size_ * count;
    void* block = malloc(total);
    blocks_.push_back(block);
    // Slots start dead (the free-list metadata lives in a separate Vector, not
    // in the block, so poisoning the whole block is safe); allocate() unpoisons.
    nw::asan::poison(block, total);
    for (std::size_t i = 0; i < count; ++i) {
        free_list_.push_back(static_cast<char*>(block) + i * block_size_);
    }
}

} // namespace detail

MemoryPool::MemoryPool(size_t max_size, size_t count, MemoryResource* allocator)
    : allocator_{allocator ? allocator : nw::kernel::global_allocator()}
    , pools_{64, allocator_}
    , max_size_(std::max(max_size, size_t(128)))
    , count_(count)
{
    // Make the smaller buckets more granular to avoid wasting too much memory
    // There is a 16 byte header containing original ptr and size.
    for (size_t size = sizeof(detail::MemoryHeader) + 8; size <= 128; size += 8) {
        pools_.emplace_back(size, count);
    }

    for (size_t size = 256; size <= max_size_; size *= 2) {
        pools_.emplace_back(size, count);
    }
}

void* MemoryPool::allocate(size_t size, size_t alignment)
{
    alignment = std::max(alignment, alignof(detail::MemoryHeader));
    size_t total_size = 0;
    if (!detail::checked_allocation_size(size, alignment, total_size)) { return nullptr; }

    void* original = nullptr;
    if (total_size <= max_size_) {
        for (size_t i = 0; i < pools_.size(); ++i) {
            if (total_size <= pools_[i].block_size()) {
                original = pools_[i].allocate();
                break;
            }
        }
    }

    // Fallback to malloc
    if (!original) { original = malloc(total_size); }
    if (!original) { return nullptr; }

    uintptr_t raw_address = reinterpret_cast<uintptr_t>(original) + sizeof(detail::MemoryHeader);
    uintptr_t aligned_address = detail::align_up(raw_address, alignment);

    detail::MemoryHeader* header = reinterpret_cast<detail::MemoryHeader*>(aligned_address - sizeof(detail::MemoryHeader));
    header->original_ptr = original;
    header->size = total_size;

    return reinterpret_cast<void*>(aligned_address);
}

void MemoryPool::deallocate(void* ptr)
{
    if (!ptr) return;
    detail::MemoryHeader* header = reinterpret_cast<detail::MemoryHeader*>(static_cast<char*>(ptr) - sizeof(detail::MemoryHeader));

    if (header->size <= max_size_) {
        for (size_t i = 0; i < pools_.size(); ++i) {
            if (header->size <= pools_[i].block_size()) {
                pools_[i].deallocate(header->original_ptr);
                return;
            }
        }
    }
    free(header->original_ptr);
}

} // namespace nw
