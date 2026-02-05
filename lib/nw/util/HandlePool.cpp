#include "HandlePool.hpp"

#include "../log.hpp"

namespace nw {

// == TypedHandlePool =========================================================

struct TypedHandleHeader {
    uint32_t generation;
    uint32_t free_list_next;
};

TypedHandlePool::TypedHandlePool(size_t object_size, size_t chunk_size)
    : object_size_(object_size)
    , objects_per_chunk_(chunk_size)
{
    LOG_F(INFO, "[TypedHandlePool] Created pool: object_size={}, chunk_size={}",
        object_size_, objects_per_chunk_);
}

TypedHandlePool::~TypedHandlePool()
{
    // Caller is responsible for destroying object contents before pool destruction
}

void TypedHandlePool::allocate_chunk()
{
    size_t stride = sizeof(TypedHandleHeader) + object_size_;
    Chunk chunk;
    chunk.memory = std::make_unique<uint8_t[]>(stride * objects_per_chunk_);
    chunk.used = 0;

    size_t base_index = chunks_.size() * objects_per_chunk_;
    chunks_.push_back(std::move(chunk));

    // Initialize header for all objects in chunk and link into free list
    // Iterate backwards so that the free list is ordered (0 -> 1 -> 2 ...)
    // causing allocations to be sequential.
    for (size_t i = objects_per_chunk_; i > 0; --i) {
        size_t current_obj = i - 1;
        uint32_t idx = static_cast<uint32_t>(base_index + current_obj);
        auto* header = static_cast<TypedHandleHeader*>(get_by_index(idx));
        if (header) {
            header->generation = 1; // Start at generation 1 (0 = invalid)
            header->free_list_next = free_list_head_;
            free_list_head_ = idx;
        }
    }
}

TypedHandle TypedHandlePool::allocate()
{
    // Allocate new chunk if needed
    if (free_list_head_ == sentinal) {
        allocate_chunk();
    }

    if (free_list_head_ == sentinal) {
        LOG_F(ERROR, "[TypedHandlePool] Failed to allocate - no free slots");
        return TypedHandle{};
    }

    // Pop from free list head
    uint32_t index = free_list_head_;
    auto* header = static_cast<TypedHandleHeader*>(get_by_index(index));
    if (!header) {
        LOG_F(ERROR, "[TypedHandlePool] Failed to get object at index {}", index);
        return TypedHandle{};
    }

    free_list_head_ = header->free_list_next;
    header->free_list_next = sentinal;
    TypedHandle result;
    result.id = index;
    result.generation = header->generation;

    uint8_t* data_start = reinterpret_cast<uint8_t*>(header) + sizeof(TypedHandleHeader);
    std::memset(data_start, 0, object_size_);

    ++live_count_;

    return result;
}

void* TypedHandlePool::get(TypedHandle h)
{
    auto* header = static_cast<TypedHandleHeader*>(get_by_index(h.id));
    if (!header) {
        return nullptr;
    }

    // Validate generation
    if (header->generation != h.generation) {
        return nullptr;
    }

    // Validate not on free list
    if (header->free_list_next != sentinal) {
        return nullptr;
    }

    return reinterpret_cast<uint8_t*>(header) + sizeof(TypedHandleHeader);
}

const void* TypedHandlePool::get(TypedHandle h) const
{
    const auto* header = static_cast<const TypedHandleHeader*>(get_by_index(h.id));
    if (!header) {
        return nullptr;
    }

    if (header->generation != h.generation) {
        return nullptr;
    }

    if (header->free_list_next != sentinal) {
        return nullptr;
    }

    return reinterpret_cast<const uint8_t*>(header) + sizeof(TypedHandleHeader);
}

bool TypedHandlePool::valid(TypedHandle h) const
{
    const auto* header = static_cast<const TypedHandleHeader*>(get_by_index(h.id));
    if (!header) {
        return false;
    }
    return header->generation == h.generation && header->free_list_next == sentinal;
}

void TypedHandlePool::destroy(TypedHandle h)
{
    if (!valid(h)) {
        LOG_F(WARNING, "[TypedHandlePool] Attempted to destroy invalid handle: id={}, gen={}",
            h.id, h.generation);
        return;
    }

    auto* header = static_cast<TypedHandleHeader*>(get_by_index(h.id));
    if (!header) {
        return;
    }

    // Caller is responsible for destroying object contents

    // Increment generation (wraps at 24-bit max)
    constexpr uint32_t max_gen = (1u << 24) - 1;
    header->generation = (header->generation + 1) & max_gen;
    if (header->generation == 0) {
        header->generation = 1; // Skip 0 (reserved for invalid)
    }

    // Add to free list head
    header->free_list_next = free_list_head_;
    free_list_head_ = h.id;

    --live_count_;
}

void* TypedHandlePool::get_by_index(uint32_t index)
{
    size_t chunk_idx = index / objects_per_chunk_;
    size_t local_idx = index % objects_per_chunk_;

    if (chunk_idx >= chunks_.size()) {
        return nullptr;
    }

    size_t stride = sizeof(TypedHandleHeader) + object_size_;
    return chunks_[chunk_idx].memory.get() + (local_idx * stride);
}

const void* TypedHandlePool::get_by_index(uint32_t index) const
{
    size_t chunk_idx = index / objects_per_chunk_;
    size_t local_idx = index % objects_per_chunk_;

    if (chunk_idx >= chunks_.size()) {
        return nullptr;
    }

    size_t stride = sizeof(TypedHandleHeader) + object_size_;
    return chunks_[chunk_idx].memory.get() + (local_idx * stride);
}

} // namespace nw
