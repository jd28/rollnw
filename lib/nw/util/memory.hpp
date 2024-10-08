#pragma once

#include "../log.hpp"
#include "templates.hpp"

#include <algorithm>
#include <assert.h>
#include <memory>
#include <memory_resource>
#include <stack>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

namespace nw {

/// Convert kilobytes to bytes
constexpr std::uint64_t KB(std::uint64_t kb)
{
    return kb * 1024ULL;
}

/// Convert megabytes to bytes
constexpr std::uint64_t MB(std::uint64_t mb)
{
    return mb * 1024ULL * 1024ULL;
}

/// Convert gigabytes to bytes
constexpr std::uint64_t GB(std::uint64_t gb)
{
    return gb * 1024ULL * 1024ULL * 1024ULL;
}

struct MemoryBlock {
    uint8_t* block = nullptr;
    size_t position = 0;
    size_t size = 0;

    auto operator<=>(const MemoryBlock&) const = default;
};

struct MemoryMarker {
    size_t chunk_index = 0;
    size_t position = 0;

    auto operator<=>(const MemoryMarker&) const = default;
};

/// A growable Memory Arena
struct MemoryArena : public std::pmr::memory_resource {
    MemoryArena(size_t blockSize = 1024);
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = default;
    virtual ~MemoryArena();

    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena& operator=(MemoryArena&&) = default;

    /// Allocate memory memory on the arena.
    void* allocate(size_t size, size_t alignment = alignof(max_align_t));

    /// Gets the current point in the allocator
    MemoryMarker current();

    /// Reset the arena, freeing all blocks except the first.
    void reset();

    /// Rewinds the allocator to a specific point in the allocator
    void rewind(MemoryMarker marker);

protected:
    // std::pmr::memory_resource interface
    void* do_allocate(size_t size, size_t alignment) override;
    void do_deallocate(void*, size_t, size_t) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

private:
    std::vector<MemoryBlock> blocks_;
    size_t current_block_ = 0;
    size_t size_ = 0;

    void alloc_block_(size_t size);
};

// This is very simple and naive.
template <typename T, size_t chunk_size>
struct ObjectPool {
    ObjectPool(MemoryArena* arena)
        : arena_{arena}
    {
    }

    T* allocate()
    {
        if (!arena_) { return nullptr; }
        if (free_list_.empty()) { allocate_chunk(); }

        auto result = free_list_.top();
        free_list_.pop();
        result = new (result) T;
        return result;
    }

    void clear()
    {
        free_list_ = std::stack<T*, std::vector<T*>>{};
        chunks_.clear();
    }

    void free(T* object)
    {
        if (!arena_) { return; }

        object->~T();
        free_list_.push(object);
    }

    void allocate_chunk()
    {
        if (arena_ == nullptr) { return; }

        T* chunk = static_cast<T*>(arena_->allocate(sizeof(T) * chunk_size, alignof(T)));
        CHECK_F(!!chunk, "Unable to allocate chunk of size {}", sizeof(T) * chunk_size);
        for (size_t i = 0; i < chunk_size; ++i) {
            free_list_.push(&chunk[i]);
        }
    }

    MemoryArena* arena_{chunk_size * sizeof(T)};
    std::stack<T*, std::vector<T*>> free_list_;
    std::vector<T*> chunks_;
};

} // namespace nw
