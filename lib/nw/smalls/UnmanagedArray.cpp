#include "UnmanagedArray.hpp"

#include "../util/macros.hpp"
#include "runtime.hpp"

namespace nw::smalls {

// == UnmanagedArray<int32_t> =================================================

template <>
void UnmanagedArray<int32_t>::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);
    if (size_ == capacity_) {
        reserve(capacity_ == 0 ? 4 : capacity_ * 2);
    }
    data_[size_++] = v.data.ival;
}

template <>
bool UnmanagedArray<int32_t>::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");
    out = Value::make_int(data_[index]);
    return true;
}

template <>
bool UnmanagedArray<int32_t>::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);
    data_[index] = v.data.ival;
    return true;
}

// == UnmanagedArray<float> ===================================================

template <>
void UnmanagedArray<float>::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);
    if (size_ == capacity_) {
        reserve(capacity_ == 0 ? 4 : capacity_ * 2);
    }
    data_[size_++] = v.data.fval;
}

template <>
bool UnmanagedArray<float>::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");
    out = Value::make_float(data_[index]);
    return true;
}

template <>
bool UnmanagedArray<float>::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);
    data_[index] = v.data.fval;
    return true;
}

// == UnmanagedArray<bool> ====================================================

template <>
void UnmanagedArray<bool>::append_value(const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);
    if (size_ == capacity_) {
        reserve(capacity_ == 0 ? 4 : capacity_ * 2);
    }
    data_[size_++] = v.data.bval;
}

template <>
bool UnmanagedArray<bool>::get_value(size_t index, Value& out, const Runtime& rt) const
{
    ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");
    out = Value::make_bool(data_[index]);
    return true;
}

template <>
bool UnmanagedArray<bool>::set_value(size_t index, const Value& v, Runtime& rt)
{
    ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");
    ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);
    data_[index] = v.data.bval;
    return true;
}

// == UnmanagedArray for POD structs ==========================================
/// Specialization for generic POD struct storage using raw bytes
template <>
class UnmanagedArray<void> : public IArray {
    uint8_t* data_ = nullptr;
    uint32_t size_ = 0;
    uint32_t capacity_ = 0;
    TypeID elem_type_;
    uint32_t elem_size_;
    uint32_t elem_alignment_;

public:
    UnmanagedArray(TypeID elem_type, uint32_t elem_size, uint32_t elem_alignment, uint32_t initial_capacity = 0)
        : elem_type_(elem_type)
        , elem_size_(elem_size)
        , elem_alignment_(std::max(1u, elem_alignment))
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

    UnmanagedArray(const UnmanagedArray&) = delete;
    UnmanagedArray& operator=(const UnmanagedArray&) = delete;

    TypeID element_type() const override { return elem_type_; }
    size_t size() const override { return size_; }
    size_t capacity() const override { return capacity_; }

    void reserve(size_t n) override
    {
        if (n <= capacity_) return;

        size_t new_capacity = n;
        if (new_capacity < 4) new_capacity = 4;

        uint8_t* new_data = static_cast<uint8_t*>(nw::kernel::global_allocator()->allocate(new_capacity * elem_size_));
        if (!new_data) { return; }
        if (data_) {
            std::memcpy(new_data, data_, size_ * elem_size_);
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
            std::memset(data_ + (size_ * elem_size_), 0, (n - size_) * elem_size_);
        }
        size_ = static_cast<uint32_t>(n);
    }

    void append_value(const Value& v, Runtime& rt) override
    {
        ENSURE_OR_RETURN(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);
        if (size_ >= capacity_) {
            reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        }

        void* src = rt.get_value_data_ptr(v);
        ENSURE_OR_RETURN(src, "UnmanagedArray::append_value: failed to get value data pointer");
        void* dst = data_ + (size_ * elem_size_);
        std::memcpy(dst, src, elem_size_);
        size_++;
    }

    bool get_value(size_t index, Value& out, const Runtime& rt) const override
    {
        ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");

        Runtime& mut_rt = const_cast<Runtime&>(rt);
        HeapPtr ptr = mut_rt.heap_.allocate(elem_size_, elem_alignment_, elem_type_);
        void* dst = mut_rt.heap_.get_ptr(ptr);
        ENSURE_OR_RETURN_FALSE(dst, "UnmanagedArray<void>::get_value failed to allocate heap value");

        const void* src = data_ + (index * elem_size_);
        std::memcpy(dst, src, elem_size_);
        out = Value::make_heap(ptr, elem_type_);
        return true;
    }

    bool set_value(size_t index, const Value& v, Runtime& rt) override
    {
        ENSURE_OR_RETURN_FALSE(index < size_, "out of bounds");
        ENSURE_OR_RETURN_FALSE(v.type_id == elem_type_, "Type mismatch: expected {}, got {}", elem_type_.value, v.type_id.value);

        void* src = rt.get_value_data_ptr(v);
        ENSURE_OR_RETURN_FALSE(src, "UnmanagedArray::set_value: failed to get value data pointer");
        void* dst = data_ + (index * elem_size_);
        std::memcpy(dst, src, elem_size_);
        return true;
    }

    const void* element_data(size_t index) const
    {
        return index < size_ ? data_ + (index * elem_size_) : nullptr;
    }
};

// == Factory Functions =======================================================

IArray* create_unmanaged_array(TypeID elem_type, uint32_t initial_capacity)
{
    const auto& rt = nw::kernel::runtime();

    TypeID declared_elem_type = elem_type;
    const Type* elem_type_info = rt.get_type(elem_type);
    while (elem_type_info
        && (elem_type_info->type_kind == TK_newtype || elem_type_info->type_kind == TK_alias)
        && elem_type_info->type_params[0].is<TypeID>()) {
        elem_type = elem_type_info->type_params[0].as<TypeID>();
        elem_type_info = rt.get_type(elem_type);
    }

    if (!elem_type_info) {
        return nullptr;
    }

    if (elem_type_info->type_kind == TK_primitive) {
        if (elem_type_info->primitive_kind == PK_int) {
            return new UnmanagedArray<int32_t>(declared_elem_type, initial_capacity);
        } else if (elem_type_info->primitive_kind == PK_float) {
            return new UnmanagedArray<float>(declared_elem_type, initial_capacity);
        } else if (elem_type_info->primitive_kind == PK_bool) {
            return new UnmanagedArray<bool>(declared_elem_type, initial_capacity);
        }
    }

    if ((elem_type_info->type_kind == TK_struct && !elem_type_info->contains_heap_refs)
        || rt.is_native_value_type(declared_elem_type)) {
        return new UnmanagedArray<void>(declared_elem_type,
            elem_type_info->size,
            elem_type_info->alignment,
            initial_capacity);
    }

    return nullptr;
}

void destroy_unmanaged_array(IArray* arr)
{
    delete arr; // Virtual destructor handles cleanup
}

} // namespace nw::smalls
