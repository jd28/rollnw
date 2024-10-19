#include "memory.hpp"

#include "../kernel/Memory.hpp"
#include "../log.hpp"

#ifdef ROLLNW_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <algorithm>

namespace nw {

namespace detail {
struct MemoryHeader {
    void* original_ptr;
    std::size_t size;
};
} // namespace detail

void* GlobalMemory::allocate(size_t size, size_t alignment)
{
    size_t total_size = size + alignment - 1 + sizeof(detail::MemoryHeader);

    void* original = malloc(total_size);
    if (!original) { return nullptr; }

    uintptr_t raw_address = reinterpret_cast<uintptr_t>(original) + sizeof(detail::MemoryHeader);
    uintptr_t aligned_address = (raw_address + alignment - 1) & ~(alignment - 1);

    detail::MemoryHeader* header = reinterpret_cast<detail::MemoryHeader*>(aligned_address - sizeof(detail::MemoryHeader));
    header->original_ptr = original;
    header->size = total_size;

    return reinterpret_cast<void*>(aligned_address);
}

void GlobalMemory::deallocate(void* ptr, size_t, size_t)
{
    if (!ptr) return;
    detail::MemoryHeader* header = reinterpret_cast<detail::MemoryHeader*>(static_cast<char*>(ptr) - sizeof(detail::MemoryHeader));
    free(header->original_ptr);
}

// == MemoryArena ==============================================================
// =============================================================================

MemoryArena::MemoryArena(size_t capacity)
    : capacity_(capacity)
{
    allocate_memory();
}

MemoryArena::~MemoryArena()
{
    deallocate_memory();
    base_ = nullptr;
}

void* MemoryArena::allocate(std::size_t size, std::size_t alignment)
{
    CHECK_F((alignment & (alignment - 1)) == 0, "Alignment must be a power of two");

    uintptr_t current = reinterpret_cast<uintptr_t>(base_) + used_;
    uintptr_t aligned = (current + (alignment - 1)) & ~(alignment - 1);
    size_t padding = aligned - current;
    int needed = padding + size;

    if (needed + used_ > capacity_) {
        return nullptr;
    }
    used_ += padding + size;

    return reinterpret_cast<void*>(aligned);
}

MemoryMarker MemoryArena::current()
{
    return {used_};
}

void MemoryArena::reset()
{
    used_ = 0;
}

void MemoryArena::rewind(MemoryMarker marker)
{
    CHECK_F(marker.position <= used_, "Memory marker mismatched");
    used_ = marker.position;
}

void MemoryArena::allocate_memory()
{
    bool check = false;
#ifdef _WIN32
    base_ = VirtualAlloc(nullptr, capacity_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check = base_ != nullptr;
#else
    base_ = mmap(nullptr, capacity_, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    check = base_ != MAP_FAILED;
#endif
    CHECK_F(!!check, "Unable to allocate block of size: {}", capacity_);
}

void MemoryArena::deallocate_memory()
{
#ifdef ROLLNW_OS_WINDOWS
    if (base_) { VirtualFree(base_, 0, MEM_RELEASE); }
#else
    if (base_) { munmap(base_, capacity_); }
#endif
    base_ = nullptr;
}

void MemoryArena::reserve_memory()
{
    bool check = false;
#ifdef _WIN32
    base_ = VirtualAlloc(nullptr, capacity_, MEM_RESERVE, PAGE_NOACCESS);
    check = !!base_;
#else
    base_ = mmap(nullptr, capacity_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    check = base_ != MAP_FAILED;
#endif
    CHECK_F(!!check, "Unable to allocate block of size: {}", capacity_);
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
    return arena_->allocate(size, alignment);
}

void MemoryScope::reset()
{
    if (arena_) {
        auto f = finalizers_;
        while (f) {
            auto obj = reinterpret_cast<uint8_t*>(f) + sizeof(detail::Finalizer);
            f->fn(obj);
            f = f->next;
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
    size_t total_size = size + alignment - 1 + sizeof(detail::MemoryHeader);
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
    if (!original) { original = malloc(size); }

    uintptr_t raw_address = reinterpret_cast<uintptr_t>(original) + sizeof(detail::MemoryHeader);
    uintptr_t aligned_address = (raw_address + alignment - 1) & ~(alignment - 1);

    detail::MemoryHeader* header = reinterpret_cast<detail::MemoryHeader*>(aligned_address - sizeof(detail::MemoryHeader));
    header->original_ptr = original;
    header->size = total_size;

    return reinterpret_cast<void*>(aligned_address);
}

void MemoryPool::deallocate(void* ptr, size_t, size_t)
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
