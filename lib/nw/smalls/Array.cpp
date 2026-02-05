#include "Array.hpp"

#include "../util/macros.hpp"
#include "runtime.hpp"

namespace nw::smalls {

// == TypedArray<int32_t> =====================================================

template <>
void TypedArray<int32_t>::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements.push_back(v.data.ival);
}

template <>
bool TypedArray<int32_t>::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    out = Value::make_int(elements[index]);
    return true;
}

template <>
bool TypedArray<int32_t>::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements[index] = v.data.ival;
    return true;
}

// == TypedArray<float> =======================================================

template <>
void TypedArray<float>::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements.push_back(v.data.fval);
}

template <>
bool TypedArray<float>::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    out = Value::make_float(elements[index]);
    return true;
}

template <>
bool TypedArray<float>::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements[index] = v.data.fval;
    return true;
}

// == TypedArray<bool> ========================================================

template <>
void TypedArray<bool>::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements.push_back(v.data.bval);
}

template <>
bool TypedArray<bool>::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    out = Value::make_bool(elements[index]);
    return true;
}

template <>
bool TypedArray<bool>::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements[index] = v.data.bval;
    return true;
}

// == TypedArray<HeapPtr> (for strings, objects, etc.) =======================

template <>
void TypedArray<HeapPtr>::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements.push_back(v.data.hptr);
}

template <>
bool TypedArray<HeapPtr>::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    out = Value::make_heap(elements[index], elem_type_id);
    return true;
}

template <>
bool TypedArray<HeapPtr>::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < elements.size(), "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);
    elements[index] = v.data.hptr;
    return true;
}

// == StructArray =============================================================

void StructArray::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);

    // Ensure capacity
    if (count >= capacity_) {
        reserve(capacity_ == 0 ? 4 : capacity_ * 2);
    }

    void* src = rt.get_value_data_ptr(v);
    ENSURE_OR_RETURN(src, "StructArray::append_value: failed to get value data pointer");
    void* dst = get_element_ptr(count);
    memcpy(dst, src, elem_size);

    count++;
}

bool StructArray::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < count, "out of bounds");

    // Allocate struct on heap and copy data (const_cast needed - heap is logically mutable)
    HeapPtr ptr = const_cast<Runtime&>(rt).heap_.allocate(elem_size, elem_alignment, elem_type_id);
    void* dst = rt.heap_.get_ptr(ptr);
    const void* src = get_element_ptr(index);
    memcpy(dst, src, elem_size);

    out = Value::make_heap(ptr, elem_type_id);
    return true;
}

bool StructArray::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < count, "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_id, "Type mismatch: expected {}, got {}", elem_type_id.value, v.type_id.value);

    void* src = rt.get_value_data_ptr(v);
    ENSURE_OR_RETURN_FALSE(src, "StructArray::set_value: failed to get value data pointer");
    void* dst = get_element_ptr(index);
    memcpy(dst, src, elem_size);

    return true;
}

} // namespace nw::smalls
