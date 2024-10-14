#pragma once

#include "../log.hpp"
#include "Allocator.hpp"

#include <memory>

namespace nw {

/// ChunkVector is an intrusively linked list of chunks of arrays.
template <typename T>
struct ChunkVector {
    using reference = T&;

    ChunkVector(size_t chunk_size, Allocator<T> allocator)
        : chunk_size_(chunk_size)
        , allocator_{allocator}
    {
    }

    ~ChunkVector()
    {
        clear();
    }

    T& operator[](size_t index)
    {
        return const_cast<T&>(static_cast<const ChunkVector&>(*this)[index]);
    }

    const T& operator[](size_t index) const
    {
        auto chunk = find_chunk(index);
        size_t ele = index % chunk_size_;
        CHECK_F(!!chunk && !!chunk->data, "attempting to address invalid chunk");
        return chunk->data[ele];
    }

    reference back()
    {
        auto chunk = find_chunk(size_ - 1);
        CHECK_F(!!chunk && !!chunk->data, "attempting to address invalid chunk");
        return chunk->data[(size_ - 1) % chunk_size_];
    }

    template <class... Args>
    reference emplace_back(Args&&... args)
    {
        push_back(T(std::forward<Args>(args)...));
        return back();
    }

    /// Clears the container
    void clear()
    {
        auto c = blocks_;
        size_t total_size = 0;
        while (c) {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (size_t i = 0; i < chunk_size_; ++i) {
                    if (total_size == size_) { break; }
                    c->data[i].~T();
                    ++total_size;
                }
            }
            std::allocator_traits<Allocator<T>>::deallocate(allocator_, c->data, chunk_size_);

            auto temp = c;
            c = c->next;
            auto ta = Allocator<Chunk>(allocator_);
            std::allocator_traits<Allocator<Chunk>>::deallocate(ta, temp, 1);
        }
        blocks_ = nullptr;
        size_ = allocated_ = 0;
    }

    /// Determines if container is empty
    bool empty() const noexcept { return size_ == 0; }

    void pop_back()
    {
        if (!blocks_) { return; }
        auto chunk = find_chunk(size_ - 1);
        CHECK_F(!!chunk && !!chunk->data, "attempting to address invalid chunk");
        chunk->data[(size_ - 1) % chunk_size_].~T();
        --size_;
    }

    void push_back(T ele)
    {
        if (!blocks_ || size_ == allocated_) { alloc_block(); }

        auto chunk = find_chunk(size_);
        CHECK_F(!!chunk && !!chunk->data, "attempting to address invalid chunk");
        new (&chunk->data[size_ % chunk_size_]) T(std::forward<T>(ele));
        ++size_;
    }

    void reserve(size_t n)
    {
        if (n < allocated_) { return; }
    }

    size_t size() const noexcept { return size_; }

private:
    struct Chunk {
        T* data = nullptr;
        Chunk* next = nullptr;
    };

    size_t chunk_size_;
    Chunk* blocks_ = nullptr;
    Chunk* tail_ = nullptr;
    size_t size_ = 0;
    size_t allocated_ = 0;
    Allocator<T> allocator_;

    void alloc_block()
    {
        auto ta = Allocator<Chunk>(allocator_);
        Chunk* chunk = std::allocator_traits<Allocator<Chunk>>::allocate(ta, 1);
        chunk->data = allocator_.allocate(chunk_size_);
        chunk->next = nullptr;

        if (!blocks_) {
            blocks_ = tail_ = chunk;
        } else {
            tail_->next = chunk;
            tail_ = chunk;
        }
        allocated_ += chunk_size_;
    }

    Chunk* find_chunk(size_t index) const noexcept
    {
        size_t chunk = index / chunk_size_;
        size_t i = 0;

        auto c = blocks_;
        while (c) {
            if (i == chunk) { break; }
            ++i;
            c = c->next;
        }

        return c;
    }
};

} // namespace nw
