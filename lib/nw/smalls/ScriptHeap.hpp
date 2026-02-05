#pragma once

#include "../../external/OffsetAllocator/offsetAllocator.hpp"
#include "../util/StrongID.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>

namespace nw::smalls {

struct GarbageCollector;

/// Type-safe heap pointer (32-bit offset into ScriptHeap)
using HeapPtr = StrongID<struct HeapPtrTag, uint32_t>;

/// Typed heap pointer with automatic dereferencing
template <typename T>
struct HeapPtrT {
    HeapPtr ptr;

    /// Get raw pointer, returns nullptr if null
    T* get() const;

    /// Implicit conversion to pointer
    operator T*() const { return get(); }

    /// Pointer-like operators
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }

    /// Null check
    explicit operator bool() const { return ptr.value != 0; }

    /// Raw heap pointer (for GC)
    HeapPtr raw() const { return ptr; }
};

enum class MarkColor : uint8_t {
    WHITE = 0,
    GRAY = 1,
    BLACK = 2
};

/// Virtual memory allocator using OffsetAllocator for free list management
struct ScriptHeap {
    struct ObjectHeader {
        OffsetAllocator::Allocation alloc;
        TypeID type_id;

        uint8_t mark_color : 2;
        uint8_t generation : 1;
        uint8_t age : 4;
        uint8_t _reserved1 : 1;

        HeapPtr next_object;
        uint32_t alloc_size;

        ObjectHeader()
            : alloc{}
            , type_id{}
            , mark_color{0}
            , generation{0}
            , age{0}
            , _reserved1{0}
            , next_object{0}
            , alloc_size{0}
        {
        }
    };

    ScriptHeap();
    ~ScriptHeap();

    ScriptHeap(const ScriptHeap&) = delete;
    ScriptHeap& operator=(const ScriptHeap&) = delete;

    /// Allocate bytes, returns heap pointer
    HeapPtr allocate(size_t size, size_t alignment = 8, TypeID type_id = TypeID{});

    /// Free allocation by heap pointer
    void free(HeapPtr ptr);

    /// Get base address
    void* base() const { return base_address_; }

    /// Convert heap pointer to raw pointer
    void* get_ptr(HeapPtr ptr) const
    {
        return base_address_ + ptr.value;
    }

    /// Convert raw pointer to heap pointer
    HeapPtr to_heap_ptr(void* ptr) const
    {
        return HeapPtr{static_cast<uint32_t>(static_cast<uint8_t*>(ptr) - base_address_)};
    }

    /// Get header for an allocation (uses back-pointer stored before user data)
    ObjectHeader* get_header(HeapPtr ptr) const
    {
        void* raw_ptr = get_ptr(ptr);
        void** back_ptr = reinterpret_cast<void**>(raw_ptr) - 1;
        return static_cast<ObjectHeader*>(*back_ptr);
    }

    /// Stats
    size_t reserved() const { return RESERVE_SIZE; }
    size_t committed() const { return committed_size_; }

    void set_gc(GarbageCollector* gc) { gc_ = gc; }
    GarbageCollector* gc() const { return gc_; }

    HeapPtr all_objects() const { return all_objects_; }
    void set_all_objects(HeapPtr head) { all_objects_ = head; }

    size_t young_bytes() const { return young_bytes_; }
    void add_young_bytes(size_t bytes) { young_bytes_ += bytes; }
    void set_young_bytes(size_t bytes) { young_bytes_ = bytes; }

    size_t old_bytes() const { return old_bytes_; }
    void add_old_bytes(size_t bytes) { old_bytes_ += bytes; }
    void set_old_bytes(size_t bytes) { old_bytes_ = bytes; }

private:
    static constexpr size_t RESERVE_SIZE = GB(2);
    static constexpr size_t MAX_ALLOCS = 1024 * 1024; // 1M allocations

    uint8_t* base_address_ = nullptr;
    size_t committed_size_ = 0;
    size_t page_size_ = 4096;
    OffsetAllocator::Allocator* allocator_ = nullptr;
    GarbageCollector* gc_ = nullptr;

    HeapPtr all_objects_{0};
    size_t young_bytes_ = 0;
    size_t old_bytes_ = 0;

    void commit_pages(size_t offset, size_t size);
};

} // namespace nw::smalls
