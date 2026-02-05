#pragma once

#include "ScriptHeap.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace nw::smalls {

struct GCRootVisitor;

struct ConfigArena {
    explicit ConfigArena(size_t initial_capacity = 64 * 1024);
    ~ConfigArena();

    ConfigArena(const ConfigArena&) = delete;
    ConfigArena& operator=(const ConfigArena&) = delete;

    void* allocate(size_t size, size_t alignment);
    void track_heap_ref(void* location_in_arena);
    void enumerate_roots(GCRootVisitor& visitor);
    void clear();

private:
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    std::vector<uint32_t> heap_ref_offsets_;
};

} // namespace nw::smalls
