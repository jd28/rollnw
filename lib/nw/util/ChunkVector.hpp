#pragma once

#include "../log.hpp"
#include "Allocator.hpp"

#include <memory>

#include <absl/container/inlined_vector.h>

namespace nw {

/// ChunkVector is an intrusively linked list of chunks of arrays.
template <typename T, size_t N = 8>
struct ChunkVector {
    using reference = T&;
    using const_reference = const T&;

    ChunkVector(size_t chunk_size, Allocator<T> allocator)
        : chunk_size_(chunk_size)
        , allocator_{allocator}
    {
    }

    ~ChunkVector()
    {
        clear();
        for (auto* it : blocks_) {
            std::allocator_traits<Allocator<T>>::deallocate(allocator_, it->data, chunk_size_);
            auto ta = Allocator<Chunk>(allocator_);
            std::allocator_traits<Allocator<Chunk>>::deallocate(ta, it, 1);
        }
    }

    reference operator[](size_t index)
    {
        return const_cast<T&>(static_cast<const ChunkVector&>(*this)[index]);
    }

    const_reference operator[](size_t index) const
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
        size_t total_size = 0;
        for (auto* it : blocks_) {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (size_t i = 0; i < chunk_size_; ++i) {
                    if (total_size == size_) { break; }
                    it->data[i].~T();
                    ++total_size;
                }
            }
        }
        size_ = 0;
    }

    void pop_back()
    {
        if (blocks_.empty()) { return; }
        auto chunk = find_chunk(size_ - 1);
        CHECK_F(!!chunk && !!chunk->data, "attempting to address invalid chunk");
        chunk->data[(size_ - 1) % chunk_size_].~T();
        --size_;
    }

    void push_back(T ele)
    {
        if (blocks_.empty() || size_ == allocated_) { alloc_block(); }

        auto chunk = find_chunk(size_);
        CHECK_F(!!chunk && !!chunk->data, "attempting to address invalid chunk");
        new (&chunk->data[size_ % chunk_size_]) T(std::move(ele));
        ++size_;
    }

    void reserve(size_t n)
    {
        if (n <= allocated_) { return; }
        while (n < allocated_) {
            alloc_block();
        }
    }

    void resize(size_t n, const T& value = T())
    {
        if (n == size_) { return; }
        if (n > size_) {
            while (size_ < n) {
                push_back(value);
            }
        } else if (n < size_) {
            while (size_ > n) {
                pop_back();
            }
        }
    }

    /// Gets container capacity
    size_t capacity() const noexcept { return allocated_; }

    /// Determines if container is empty
    bool empty() const noexcept { return size_ == 0; }

    /// Gets container size
    size_t size() const noexcept { return size_; }

    template <bool IsConst>
    struct Iterator;

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, size()); }

    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size()); }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

private:
    struct Chunk {
        T* data = nullptr;
    };

    absl::InlinedVector<Chunk*, N> blocks_;
    size_t chunk_size_;
    size_t size_ = 0;
    size_t allocated_ = 0;
    Allocator<T> allocator_;

    void alloc_block()
    {
        auto ta = Allocator<Chunk>(allocator_);
        Chunk* chunk = std::allocator_traits<Allocator<Chunk>>::allocate(ta, 1);
        chunk->data = allocator_.allocate(chunk_size_);
        blocks_.push_back(chunk);
        allocated_ += chunk_size_;
    }

    Chunk* find_chunk(size_t index) const noexcept
    {
        size_t chunk = index / chunk_size_;
        CHECK_F(chunk < blocks_.size(), "out of bounds");
        return blocks_[chunk];
    }
};

template <typename T, size_t N>
template <bool IsConst>
struct ChunkVector<T, N>::Iterator {
    using VecType = std::conditional_t<IsConst, const ChunkVector, ChunkVector>;
    using value_type = std::conditional_t<IsConst, const T, T>;
    using reference = value_type&;
    using pointer = value_type*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    Iterator()
        : vec_{nullptr}
        , index_{0}
    {
    }

    Iterator(VecType* vec, size_t index)
        : vec_{vec}
        , index_{index}
    {
    }

    reference operator*() const { return (*vec_)[index_]; }
    pointer operator->() const { return &(*vec_)[index_]; }

    Iterator& operator++()
    {
        ++index_;
        return *this;
    }
    Iterator operator++(int)
    {
        auto tmp = *this;
        ++index_;
        return tmp;
    }

    Iterator& operator--()
    {
        --index_;
        return *this;
    }
    Iterator operator--(int)
    {
        auto tmp = *this;
        --index_;
        return tmp;
    }

    Iterator& operator+=(difference_type n)
    {
        index_ += n;
        return *this;
    }
    Iterator& operator-=(difference_type n)
    {
        index_ -= n;
        return *this;
    }

    Iterator operator+(difference_type n) const { return {vec_, index_ + n}; }
    Iterator operator-(difference_type n) const { return {vec_, index_ - n}; }
    difference_type operator-(const Iterator& other) const { return index_ - other.index_; }

    reference operator[](difference_type n) const { return (*vec_)[index_ + n]; }

    bool operator==(const Iterator& other) const { return index_ == other.index_; }
    bool operator!=(const Iterator& other) const { return index_ != other.index_; }
    bool operator<(const Iterator& other) const { return index_ < other.index_; }
    bool operator>(const Iterator& other) const { return index_ > other.index_; }
    bool operator<=(const Iterator& other) const { return index_ <= other.index_; }
    bool operator>=(const Iterator& other) const { return index_ >= other.index_; }

    VecType* vec_;
    size_t index_;
};

} // namespace nw
