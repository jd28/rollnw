#pragma once

#include "Array.hpp"

#include "../kernel/Memory.hpp"

#include <cstdint>
#include <cstring>

namespace nw::smalls {

/// Unmanaged array for propset storage - allocated via engine allocator, not GC
/// Template parameter T must be POD (no heap references, no destructors)
template <typename T>
class UnmanagedArray : public IArray {
    T* data_ = nullptr;
    uint32_t size_ = 0;
    uint32_t capacity_ = 0;
    TypeID elem_type_;

public:
    explicit UnmanagedArray(TypeID elem_type, uint32_t initial_capacity = 0)
        : elem_type_(elem_type)
    {
        if (initial_capacity > 0) {
            reserve(initial_capacity);
        }
    }

    ~UnmanagedArray() override
    {
        if (data_) {
            nw::kernel::global_allocator()->deallocate(data_);
            data_ = nullptr;
        }
    }

    // Non-copyable, non-movable (handles are the identity)
    UnmanagedArray(const UnmanagedArray&) = delete;
    UnmanagedArray& operator=(const UnmanagedArray&) = delete;
    UnmanagedArray(UnmanagedArray&&) = delete;
    UnmanagedArray& operator=(UnmanagedArray&&) = delete;

    // IArray interface
    TypeID element_type() const override { return elem_type_; }
    size_t size() const override { return size_; }
    size_t capacity() const override { return capacity_; }

    void reserve(size_t n) override
    {
        if (n <= capacity_) return;

        size_t new_capacity = n;
        if (new_capacity < 4) new_capacity = 4;

        T* new_data = static_cast<T*>(nw::kernel::global_allocator()->allocate(new_capacity * sizeof(T)));
        if (!new_data) { return; }
        if (data_) {
            std::memcpy(new_data, data_, size_ * sizeof(T));
            nw::kernel::global_allocator()->deallocate(data_);
        }
        data_ = new_data;
        capacity_ = static_cast<uint32_t>(new_capacity);
    }

    void clear() override { size_ = 0; }

    void resize(size_t n) override
    {
        if (n > capacity_) {
            reserve(n);
        }
        if (n > size_) {
            std::memset(data_ + size_, 0, (n - size_) * sizeof(T));
        }
        size_ = static_cast<uint32_t>(n);
    }

    void append_value(const Value& v, Runtime& rt) override;
    bool get_value(size_t index, Value& out, const Runtime& rt) const override;
    bool set_value(size_t index, const Value& v, Runtime& rt) override;

    // Direct data access for engine use
    T* data() { return data_; }
    const T* data() const { return data_; }
    T* at(size_t index) { return index < size_ ? data_ + index : nullptr; }
    const T* at(size_t index) const { return index < size_ ? data_ + index : nullptr; }
};

/// Factory for creating type-erased unmanaged arrays
/// Returns IArray* that must be stored in the unmanaged array pool
IArray* create_unmanaged_array(TypeID elem_type, uint32_t initial_capacity);

/// Destroy an unmanaged array (calls delete, which handles deallocation)
void destroy_unmanaged_array(IArray* arr);

} // namespace nw::smalls
