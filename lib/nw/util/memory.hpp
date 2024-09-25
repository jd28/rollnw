#pragma once

#include "templates.hpp"

#include <algorithm>
#include <assert.h>
#include <memory>
#include <stack>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

namespace nw {

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
        for (size_t i = 0; i < chunk_size; ++i) {
            free_list_.push(&chunk[i]);
        }
    }

    MemoryArena* arena_{chunk_size * sizeof(T)};
    std::stack<T*, std::vector<T*>> free_list_;
    std::vector<T*> chunks_;
};

} // namespace nw
