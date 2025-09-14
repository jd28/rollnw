#pragma once

#include "../kernel/Memory.hpp"
#include "ChunkVector.hpp"

#include <cstdint>
#include <utility>

namespace nw {

template <typename HandleType, typename Impl>
struct HandlePool {
    static constexpr typename HandleType::size_type sentinal = std::numeric_limits<typename HandleType::size_type>::max();

    HandlePool(size_t chunk_size, nw::MemoryResource* allocator = nw::kernel::global_allocator())
        : storage_{chunk_size, allocator}
    {
    }

    Impl* create()
    {
        return get(insert());
    }

    Impl* get(HandleType hndl)
    {
        if (!valid(hndl)) { return nullptr; }
        auto& payload = storage_[hndl.index];
        return &payload.value;
    }

    HandleType insert()
    {
        typename HandleType::size_type idx = 0;
        typename HandleType::size_type gen = 1;
        Impl* impl = nullptr;

        if (free_list_head_ != sentinal) {
            idx = free_list_head_;
            free_list_head_ = storage_[idx].free_list_next;
            storage_[idx].free_list_next = sentinal;
            gen = storage_[idx].gen;
        } else {
            idx = static_cast<typename HandleType::size_type>(storage_.size());
            auto& payload = storage_.emplace_back(ImplPayload{});
            payload.gen = 1;
            payload.free_list_next = sentinal;
        }
        impl = &storage_[idx].value;

        HandleType result;
        result.generation = gen;
        result.index = idx;
        impl->set_handle(result);
        return result;
    }

    void destroy(HandleType hndl)
    {
        if (!valid(hndl)) { return; }

        auto& payload = storage_[hndl.index];
        if (payload.gen != hndl.generation) { return; }
        payload.value.reset();
        ++payload.gen;
        payload.free_list_next = free_list_head_;
        free_list_head_ = hndl.index;
    }

    bool valid(HandleType hndl) const noexcept
    {
        if (hndl.generation == 0) { return false; }
        if (hndl.index >= storage_.size()) { return false; }
        return storage_[hndl.index].gen == hndl.generation;
    }

private:
    struct ImplPayload {
        Impl value;
        typename HandleType::size_type gen = 1;
        typename HandleType::size_type free_list_next = sentinal;
    };

    ChunkVector<ImplPayload> storage_;
    typename HandleType::size_type free_list_head_ = sentinal;
};

} // namespace nw
