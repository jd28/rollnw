#pragma once

#include "templates.hpp"

#include <memory>
#include <stack>
#include <vector>

namespace nw {

// This is very simple and naive.
template <typename T, size_t chunk_size>
struct ObjectPool {
    struct Chunk {
        std::array<T, chunk_size> objects;
    };

    T* allocate()
    {
        if (free_list.empty()) {
            chunks.push_back(std::make_unique<Chunk>());
            for (auto& object : reverse(chunks.back()->objects)) {
                free_list.push(&object);
            }
        }
        auto result = free_list.top();
        free_list.pop();
        result->~T();
        result = new (result) T;
        return result;
    }

    void clear()
    {
        free_list = std::stack<T*, std::vector<T*>>{};
        chunks.clear();
    }

    void free(T* object)
    {
        free_list.push(object);
    }

    std::stack<T*, std::vector<T*>> free_list;
    std::vector<std::unique_ptr<Chunk>> chunks;
};

} // namespace nw
