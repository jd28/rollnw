#pragma once

#include "../kernel/Memory.hpp"
#include "ChunkVector.hpp"

#include <cstdint>
#include <utility>

namespace nw {

struct Handle {
    uint32_t generation = 0;
    uint32_t id = 0;

    bool operator==(const Handle&) const = default;
    auto operator<=>(const Handle&) const = default;

    template <typename H>
    friend H AbslHashValue(H h, const Handle& handle)
    {
        return H::combine(std::move(h), handle.generation, handle.id);
    }
};

template <typename Payload>
struct HandlePool {
    static constexpr uint32_t sentinal = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t max_generation = std::numeric_limits<uint32_t>::max();

    HandlePool(size_t chunk_size, nw::MemoryResource* allocator = nw::kernel::global_allocator())
        : storage_{chunk_size, allocator}
    {
    }

    Payload* create()
    {
        return get(insert());
    }

    Payload* get(Handle h) const
    {
        if (!valid(h)) { return nullptr; }
        auto& payload = storage_[h.id];
        return &payload.value;
    }

    Payload* get(Handle h)
    {
        if (!valid(h)) { return nullptr; }
        auto& payload = storage_[h.id];
        return &payload.value;
    }

    Handle insert()
    {
        uint32_t idx = 0;
        uint32_t gen = 1;
        Payload* payload_ptr = nullptr;

        if (free_list_head_ != sentinal) {
            idx = free_list_head_;
            free_list_head_ = storage_[idx].free_list_next;
            storage_[idx].free_list_next = sentinal;
            gen = storage_[idx].gen;
        } else {
            idx = static_cast<uint32_t>(storage_.size());
            auto& payload = storage_.emplace_back(PayloadEntry{});
            payload.gen = 1;
            payload.free_list_next = sentinal;
        }
        payload_ptr = &storage_[idx].value;

        Handle result;
        result.generation = gen;
        result.id = idx;
        payload_ptr->set_generic_handle(result);
        return result;
    }

    void destroy(Handle h)
    {
        if (!valid(h)) { return; }

        auto& payload = storage_[h.id];
        if (payload.gen != h.generation) { return; }
        payload.value.reset();
        ++payload.gen;
        payload.free_list_next = free_list_head_;
        free_list_head_ = h.id;
    }

    bool valid(Handle h) const noexcept
    {
        if (h.generation == 0) { return false; }
        if (h.id >= storage_.size()) { return false; }
        return storage_[h.id].gen == h.generation;
    }

private:
    struct PayloadEntry {
        Payload value;
        uint32_t gen = 1;
        uint32_t free_list_next = sentinal;
    };

    ChunkVector<PayloadEntry> storage_;
    uint32_t free_list_head_ = sentinal;
};

// == TypedHandle =============================================================
// ============================================================================

/// Handle for runtime objects (effects, events, etc.)
/// Embeds type information like ObjectHandle does
struct TypedHandle {
    uint32_t id : 32 = 0;         // Pool index
    uint8_t type : 8 = 0;         // Object type (1=Effect, 2=Event, etc.)
    uint32_t generation : 24 = 0; // Anti-ABA

    bool operator==(const TypedHandle&) const = default;
    auto operator<=>(const TypedHandle&) const = default;

    /// Convert to 64-bit integer for VM storage
    uint64_t to_ull() const noexcept
    {
        return (static_cast<uint64_t>(id) << 32)
            | (static_cast<uint64_t>(type) << 24)
            | static_cast<uint64_t>(generation);
    }

    /// Convert from 64-bit integer
    static TypedHandle from_ull(uint64_t val) noexcept
    {
        TypedHandle h;
        h.id = static_cast<uint32_t>(val >> 32);
        h.type = static_cast<uint8_t>((val >> 24) & 0xFF);
        h.generation = static_cast<uint32_t>(val & 0xFFFFFF);
        return h;
    }

    bool is_valid() const noexcept { return *this != TypedHandle{}; }
};

template <typename H>
H AbslHashValue(H h, const TypedHandle& handle)
{
    return H::combine(std::move(h), handle.id, handle.type, handle.generation);
}

/// Generic pool for fixed-size objects with intrusive free list
/// Allocates N-byte objects, returns void* - caller interprets bytes
struct TypedHandlePool {
    static constexpr uint32_t sentinal = std::numeric_limits<uint32_t>::max();

    /// Constructor - just needs object size
    /// @param object_size Size of each object in bytes
    /// @param chunk_size Number of objects per chunk
    TypedHandlePool(size_t object_size, size_t chunk_size = 1024);
    ~TypedHandlePool();

    /// Allocates a new object, returns handle (caller sets type field)
    TypedHandle allocate();

    /// Gets object by handle (returns generic pointer)
    void* get(TypedHandle h);
    const void* get(TypedHandle h) const;

    /// Validates handle (checks generation)
    bool valid(TypedHandle h) const;

    /// Destroys object (caller responsible for cleanup)
    void destroy(TypedHandle h);

    /// Gets the number of live objects
    size_t size() const { return live_count_; }

    /// Gets the total capacity across all chunks
    size_t capacity() const { return objects_per_chunk_ * chunks_.size(); }

private:
    struct Chunk {
        std::unique_ptr<uint8_t[]> memory;
        size_t used = 0;
    };

    void allocate_chunk();
    void* get_by_index(uint32_t index);
    const void* get_by_index(uint32_t index) const;

    size_t object_size_;       // Size of each object in bytes
    size_t objects_per_chunk_; // How many objects fit in one chunk

    Vector<Chunk> chunks_;
    uint32_t free_list_head_ = sentinal; // Intrusive free list head
    size_t live_count_ = 0;
};

} // namespace nw
