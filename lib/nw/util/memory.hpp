#pragma once

#include "../log.hpp"
#include "templates.hpp"

#include <algorithm>
#include <assert.h>
#include <memory>
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

/// A growable Memory Arena
struct MemoryArena {
    MemoryArena(size_t blockSize = 1024);
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = default;
    ~MemoryArena();

    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena& operator=(MemoryArena&&) = default;

    /// Allocate memory memory on the arena.
    void* allocate(size_t size, size_t alignment = alignof(max_align_t));

    /// Reset the arena, freeing all blocks except the first.
    void reset();

private:
    size_t size_;
    std::vector<void*> blocks_;
    uint8_t* block_;
    size_t pos_;
    size_t end_;

    void alloc_block_(size_t size);
};

/// C++ Allocator interface for Memory arena
template <typename T>
class ArenaAllocator {
public:
    using value_type = T;
    ArenaAllocator(MemoryArena* arena)
        : arena_(arena)
    {
    }

    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& other)
        : arena_(other.arena_)
    {
    }

    /// Allocate memory for n objects of type T.
    T* allocate(size_t n)
    {
        if (!arena_) { return nullptr; }
        size_t size = n * sizeof(T);
        void* ptr = arena_->allocate(size * sizeof(T), alignof(T));
        return static_cast<T*>(ptr);
    }

    /// Deallocate memory. a no-op.
    void deallocate(T*, size_t) { }

    template <typename U>
    bool operator==(const ArenaAllocator<U>& other) const
    {
        return arena_ == other.arena_;
    }

    template <typename U>
    bool operator!=(const ArenaAllocator<U>& other) const
    {
        return !(*this == other);
    }

private:
    MemoryArena* arena_ = nullptr;
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
