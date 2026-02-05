#include "ConfigArena.hpp"

#include "VirtualMachine.hpp"

#include <cstdlib>

namespace nw::smalls {

ConfigArena::ConfigArena(size_t initial_capacity)
    : capacity_(initial_capacity)
{
    data_ = static_cast<uint8_t*>(std::malloc(capacity_));
    if (data_) {
        std::memset(data_, 0, capacity_);
    }
}

ConfigArena::~ConfigArena()
{
    std::free(data_);
}

void* ConfigArena::allocate(size_t size, size_t alignment)
{
    size_t offset = (size_ + alignment - 1) & ~(alignment - 1);
    if (offset + size > capacity_) {
        size_t new_capacity = capacity_ * 2;
        while (offset + size > new_capacity) {
            new_capacity *= 2;
        }
        uint8_t* new_data = static_cast<uint8_t*>(std::realloc(data_, new_capacity));
        if (!new_data) {
            return nullptr;
        }
        std::memset(new_data + capacity_, 0, new_capacity - capacity_);
        data_ = new_data;
        capacity_ = new_capacity;
    }

    void* ptr = data_ + offset;
    size_ = offset + size;
    return ptr;
}

void ConfigArena::track_heap_ref(void* location_in_arena)
{
    auto* byte_ptr = static_cast<uint8_t*>(location_in_arena);
    uint32_t offset = static_cast<uint32_t>(byte_ptr - data_);
    heap_ref_offsets_.push_back(offset);
}

void ConfigArena::enumerate_roots(GCRootVisitor& visitor)
{
    for (uint32_t offset : heap_ref_offsets_) {
        if (offset < size_) {
            HeapPtr* ptr = reinterpret_cast<HeapPtr*>(data_ + offset);
            if (ptr->value != 0) {
                visitor.visit_root(ptr);
            }
        }
    }
}

void ConfigArena::clear()
{
    size_ = 0;
    heap_ref_offsets_.clear();
    if (data_) {
        std::memset(data_, 0, capacity_);
    }
}

} // namespace nw::smalls
