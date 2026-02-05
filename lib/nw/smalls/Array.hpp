#pragma once

#include "types.hpp"

#include <cstdint>
#include <cstring>

namespace nw::smalls {

// Forward declarations
struct Runtime;
struct Value;

/// Abstract interface for script arrays with runtime type dispatch
/// Allows specialized storage per element type while maintaining polymorphic access
struct IArray {
    virtual ~IArray() = default;

    /// Get the TypeID of array elements
    virtual TypeID element_type() const = 0;

    /// Get the number of elements in the array
    virtual size_t size() const = 0;

    /// Get capacity (allocated size)
    virtual size_t capacity() const = 0;

    /// Reserve space for at least n elements
    virtual void reserve(size_t n) = 0;

    /// Append a value (wrapped as Value)
    virtual void append_value(const Value& v, Runtime& rt) = 0;

    /// Get element at index (returns as Value)
    /// @return true if index valid, false otherwise
    virtual bool get_value(size_t index, Value& out, const Runtime& rt) const = 0;

    /// Set element at index
    /// @return true if index valid and type matches, false otherwise
    virtual bool set_value(size_t index, const Value& v, Runtime& rt) = 0;

    /// Clear all elements
    virtual void clear() = 0;

    /// Resize array to n elements (zero-initializes new elements)
    virtual void resize(size_t n) = 0;
};

/// Typed array with native storage for primitives and inline structs
/// Template parameter T can be: int32_t, float, bool, or value_type structs
template <typename T>
struct TypedArray : IArray {
    TypeID elem_type_id;
    Vector<T> elements;

    explicit TypedArray(TypeID type_id, size_t initial_capacity = 0)
        : elem_type_id(type_id)
    {
        if (initial_capacity > 0) {
            elements.reserve(initial_capacity);
        }
    }

    TypeID element_type() const override { return elem_type_id; }
    size_t size() const override { return elements.size(); }
    size_t capacity() const override { return elements.capacity(); }
    void reserve(size_t n) override { elements.reserve(n); }
    void clear() override { elements.clear(); }
    void resize(size_t n) override { elements.resize(n, T{}); }

    void append_value(const Value& v, Runtime& rt) override;
    bool get_value(size_t index, Value& out, const Runtime& rt) const override;
    bool set_value(size_t index, const Value& v, Runtime& rt) override;
};

/// Generic struct array using raw bytes for script-defined [[value_type]] structs
/// This handles any struct where we only know size/alignment at runtime
struct StructArray : IArray {
    TypeID elem_type_id;
    uint32_t elem_size;
    uint32_t elem_alignment;
    size_t count;
    size_t capacity_;
    Vector<uint8_t> buffer; // Raw byte storage

    StructArray(TypeID type_id, uint32_t size, uint32_t alignment, size_t initial_capacity = 0)
        : elem_type_id(type_id)
        , elem_size(size)
        , elem_alignment(std::max(1u, alignment))
        , count(0)
        , capacity_(initial_capacity)
    {
        if (initial_capacity > 0) {
            buffer.resize(initial_capacity * elem_size, 0);
        }
    }

    TypeID element_type() const override { return elem_type_id; }
    size_t size() const override { return count; }
    size_t capacity() const override { return capacity_; }

    void reserve(size_t n) override
    {
        if (n > capacity_) {
            buffer.resize(n * elem_size, 0);
            capacity_ = n;
        }
    }

    void clear() override
    {
        count = 0;
        memset(buffer.data(), 0, buffer.size());
    }

    void resize(size_t n) override
    {
        if (n > capacity_) {
            reserve(n * 2);
        }
        if (n > count) {
            memset(buffer.data() + (count * elem_size), 0, (n - count) * elem_size);
        }
        count = n;
    }

    void append_value(const Value& v, Runtime& rt) override;
    bool get_value(size_t index, Value& out, const Runtime& rt) const override;
    bool set_value(size_t index, const Value& v, Runtime& rt) override;

    const void* element_data(size_t index) const
    {
        return index < count ? buffer.data() + (index * elem_size) : nullptr;
    }

private:
    uint8_t* get_element_ptr(size_t index)
    {
        return buffer.data() + (index * elem_size);
    }

    const uint8_t* get_element_ptr(size_t index) const
    {
        return buffer.data() + (index * elem_size);
    }
};

} // namespace nw::smalls
