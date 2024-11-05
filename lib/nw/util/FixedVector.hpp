#pragma once

#include "../kernel/Memory.hpp"
#include "Allocator.hpp"

namespace nw {

/// FixedVector provides the interface of std::vector over a fixed heap allocated buffer
template <typename T, typename Alloc = Allocator<T>>
struct FixedVector {
    using value_type = T;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;
    using allocator_type = Alloc;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;

    FixedVector(size_type capacity, Alloc allocator = Alloc())
        : capacity_(capacity)
        , allocator_(allocator)
    {
        buffer_ = allocator_.allocate(capacity_);
    }

    ~FixedVector()
    {
        clear();
        allocator_.deallocate(buffer_, capacity_);
    }

    FixedVector(const FixedVector& other)
        : size_(other.size_)
        , capacity_(other.capacity_)
        , allocator_(other.allocator_)
    {
        buffer_ = allocator_.allocate(capacity_);
        std::uninitialized_copy(other.buffer_, other.buffer_ + size_, buffer_);
    }

    FixedVector& operator=(const FixedVector& other)
    {
        if (this != &other) {
            clear();
            allocator_.deallocate(buffer_, capacity_);

            capacity_ = other.capacity_;
            size_ = other.size_;
            allocator_ = other.allocator_;
            buffer_ = allocator_.allocate(capacity_);
            std::uninitialized_copy(other.buffer_, other.buffer_ + size_, buffer_);
        }
        return *this;
    }

    FixedVector(FixedVector&& other) noexcept
        : buffer_(other.buffer_)
        , size_(other.size_)
        , capacity_(other.capacity_)
        , allocator_(std::move(other.allocator_))
    {

        other.buffer_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    FixedVector& operator=(FixedVector&& other) noexcept
    {
        if (this != &other) {
            clear();
            allocator_.deallocate(buffer_, capacity_);

            buffer_ = other.buffer_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            allocator_ = std::move(other.allocator_);

            other.buffer_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    reference operator[](size_type index)
    {
        return buffer_[index];
    }

    const_reference operator[](size_type index) const
    {
        return buffer_[index];
    }

    void clear()
    {
        for (size_t i = 0; i < size_; ++i) {
            buffer_[i].~T();
        }
        size_ = 0;
    }

    reference front()
    {
        CHECK_F(!empty(), "out of boundss");
        return buffer_[0];
    }

    const_reference front() const
    {
        CHECK_F(!empty(), "out of boundss");
        return buffer_[0];
    }

    reference back()
    {
        CHECK_F(!empty(), "out of boundss");
        return buffer_[size_ - 1];
    }

    const_reference back() const
    {
        CHECK_F(!empty(), "out of boundss");
        return buffer_[size_ - 1];
    }

    T* data() noexcept
    {
        return buffer_;
    }

    const T* data() const noexcept
    {
        return buffer_;
    }

    template <typename... Args>
    void emplace_back(Args&&... args)
    {
        CHECK_F(size_ < capacity_, "FixedVector capacity exceeded");
        new (&buffer_[size_]) T(std::forward<Args>(args)...);
        ++size_;
    }

    iterator erase(iterator pos)
    {
        CHECK_F(pos >= begin() && pos < end(), "Erase position out of range");

        size_type index = pos - begin();
        buffer_[index].~T();

        for (size_type i = index; i < size_ - 1; ++i) {
            new (&buffer_[i]) T(std::move(buffer_[i + 1]));
            buffer_[i + 1].~T();
        }

        --size_;
        return buffer_ + index;
    }

    iterator erase(iterator first, iterator last)
    {
        CHECK_F(first >= begin() && last <= end() && first <= last, "Invalid range for erase");

        size_type erase_count = last - first;
        size_type index = first - begin();

        for (iterator it = first; it != last; ++it) {
            it->~T();
        }

        for (size_type i = index; i + erase_count < size_; ++i) {
            new (&buffer_[i]) T(std::move(buffer_[i + erase_count]));
            buffer_[i + erase_count].~T();
        }

        size_ -= erase_count;
        return buffer_ + index;
    }

    iterator insert(iterator pos, const T& value)
    {
        CHECK_F(size_ < capacity_, "FixedVector capacity exceeded");
        size_type index = pos - buffer_;

        for (size_type i = size_; i > index; --i) {
            new (&buffer_[i]) T(std::move(buffer_[i - 1]));
            buffer_[i - 1].~T();
        }

        new (&buffer_[index]) T(value);

        ++size_;
        return buffer_ + index;
    }

    iterator insert(iterator pos, T&& value)
    {
        if (size_ >= capacity_) {
            throw std::out_of_range("FixedVector capacity exceeded");
        }

        size_type index = pos - buffer_;

        for (size_type i = size_; i > index; --i) {
            new (&buffer_[i]) T(std::move(buffer_[i - 1]));
            buffer_[i - 1].~T();
        }

        new (&buffer_[index]) T(std::move(value));

        ++size_;
        return buffer_ + index;
    }
    void pop_back()
    {
        CHECK_F(!empty(), "out of boundss");
        buffer_[size_ - 1].~T();
        --size_;
    }

    void push_back(const T& value)
    {
        CHECK_F(size_ < capacity_, "FixedVector capacity exceeded");
        new (&buffer_[size_]) T(value);
        ++size_;
    }

    void push_back(T&& value)
    {
        CHECK_F(size_ < capacity_, "FixedVector capacity exceeded");
        new (&buffer_[size_]) T(std::move(value));
        ++size_;
    }

    void resize(size_type new_size, const T& value = T())
    {
        CHECK_F(size_ <= capacity_, "FixedVector capacity exceeded");

        while (size_ < new_size)
            push_back(value);

        while (size_ > new_size)
            pop_back();
    }

    iterator begin() noexcept { return buffer_; }
    const_iterator begin() const noexcept { return buffer_; }
    const_iterator cbegin() const noexcept { return buffer_; }

    iterator end() noexcept { return buffer_ + size_; }
    const_iterator end() const noexcept { return buffer_ + size_; }
    const_iterator cend() const noexcept { return buffer_ + size_; }

    bool empty() const { return size_ == 0; }
    size_type size() const { return size_; }
    size_type max_size() const { return capacity_; }
    size_type capacity() const { return capacity_; }

private:
    T* buffer_;
    size_t size_ = 0;
    size_t capacity_ = 0;
    Alloc allocator_;
};

}
