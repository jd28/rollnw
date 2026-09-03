#include "ObjectValueComponent.hpp"

#include "runtime.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace nw::smalls {

namespace {

class ComponentArray final : public IArray {
public:
    ComponentArray(Runtime& runtime, ObjectValueComponent& component, ObjectValueOwner owner,
        TypeID element_type, const Type& element_info, size_t initial_capacity)
        : runtime_{runtime}
        , component_{component}
        , owner_{owner}
        , element_type_{element_type}
        , element_size_{element_info.size}
    {
        reserve(initial_capacity);
    }

    TypeID element_type() const override { return element_type_; }
    size_t size() const override { return size_; }
    size_t capacity() const override { return capacity_; }

    void reserve(size_t count) override
    {
        if (count <= capacity_) { return; }
        if (element_size_ == 0
            || count > std::numeric_limits<size_t>::max() / element_size_) {
            return;
        }

        const size_t old_bytes = data_.size();
        data_.resize(count * element_size_);
        std::memset(data_.data() + old_bytes, 0, data_.size() - old_bytes);
        capacity_ = count;
    }

    void append_value(const Value& value, Runtime& runtime) override
    {
        if (value.type_id != element_type_) {
            runtime.fail("object component array append type mismatch");
            return;
        }
        if (size_ == capacity_) {
            if (capacity_ > std::numeric_limits<size_t>::max() / 2) {
                runtime.fail("object component array capacity exhausted");
                return;
            }
            reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        }
        if (size_ == capacity_) {
            runtime.fail("object component array capacity exhausted");
            return;
        }

        uint8_t* destination = data_.data() + size_ * element_size_;
        if (!runtime.copy_value_to_object_component(
                component_, owner_, element_type_, value, destination)) {
            runtime.fail("object component array could not copy value");
            return;
        }
        ++size_;
    }

    bool get_value(size_t index, Value& output, const Runtime& runtime) const override
    {
        if (index >= size_) { return false; }
        output = const_cast<Runtime&>(runtime).materialize_inline_value(
            data_.data() + index * element_size_, element_type_);
        return output.type_id == element_type_;
    }

    bool set_value(size_t index, const Value& value, Runtime& runtime) override
    {
        if (index >= size_ || value.type_id != element_type_) { return false; }

        std::vector<uint8_t> replacement(element_size_);
        if (!runtime.copy_value_to_object_component(
                component_, owner_, element_type_, value, replacement.data())) {
            return false;
        }
        std::memcpy(data_.data() + index * element_size_, replacement.data(), element_size_);
        return true;
    }

    void clear() override
    {
        size_ = 0;
        if (!data_.empty()) {
            std::memset(data_.data(), 0, data_.size());
        }
    }

    void resize(size_t count) override
    {
        if (count > capacity_) { reserve(count); }
        if (count > capacity_) { return; }

        for (size_t index = size_; index < count; ++index) {
            uint8_t* destination = data_.data() + index * element_size_;
            std::memset(destination, 0, element_size_);
            if (!runtime_.initialize_object_component_value(
                    component_, owner_, element_type_, destination)) {
                return;
            }
        }
        if (count < size_) {
            std::memset(data_.data() + count * element_size_, 0,
                (size_ - count) * element_size_);
        }
        size_ = count;
    }

    const void* element_data(size_t index) const override
    {
        return index < size_ ? data_.data() + index * element_size_ : nullptr;
    }

    size_t retained_bytes() const noexcept override
    {
        return sizeof(ComponentArray) + data_.capacity();
    }

private:
    Runtime& runtime_;
    ObjectValueComponent& component_;
    ObjectValueOwner owner_{};
    TypeID element_type_ = invalid_type_id;
    uint32_t element_size_ = 0;
    size_t size_ = 0;
    size_t capacity_ = 0;
    std::vector<uint8_t> data_;
};

} // namespace

HeapPtr ObjectValueComponent::create_array(Runtime& runtime, ObjectValueOwner owner,
    TypeID element_type, size_t initial_capacity)
{
    const Type* element_info = runtime.get_type(element_type);
    if (!element_info || element_info->size == 0
        || !runtime.type_table_.is_object_component_value(element_type)) {
        runtime.fail("unsupported object component array element type");
        return {};
    }

    auto array = std::make_unique<ComponentArray>(
        runtime, *this, owner, element_type, *element_info, initial_capacity);
    HeapPtr result = insert(owner, std::move(array));
    if (result.value == 0) {
        runtime.fail("object component handle space exhausted");
    }
    return result;
}

IArray* ObjectValueComponent::get_array(HeapPtr ptr) noexcept
{
    if (!is_component_ptr(ptr)) { return nullptr; }
    const uint32_t index = ptr.value & index_mask;
    if (index == 0 || index > entries_.size()) { return nullptr; }
    return entries_[index - 1].array.get();
}

const IArray* ObjectValueComponent::get_array(HeapPtr ptr) const noexcept
{
    return const_cast<ObjectValueComponent*>(this)->get_array(ptr);
}

const ObjectValueOwner* ObjectValueComponent::owner(HeapPtr ptr) const noexcept
{
    if (!is_component_ptr(ptr)) { return nullptr; }
    const uint32_t index = ptr.value & index_mask;
    if (index == 0 || index > entries_.size() || !entries_[index - 1].array) {
        return nullptr;
    }
    return &entries_[index - 1].owner;
}

void ObjectValueComponent::release(ObjectHandle object) noexcept
{
    auto found = object_nodes_.find(object.to_ull());
    if (found == object_nodes_.end()) { return; }

    for (uint32_t index : found->second) {
        if (index < entries_.size() && entries_[index].array) {
            entries_[index].array.reset();
            --live_nodes_;
        }
    }
    object_nodes_.erase(found);
}

size_t ObjectValueComponent::retained_bytes() const noexcept
{
    size_t result = entries_.capacity() * sizeof(Entry);
    for (const Entry& entry : entries_) {
        if (entry.array) { result += entry.array->retained_bytes(); }
    }
    return result;
}

HeapPtr ObjectValueComponent::insert(ObjectValueOwner owner, std::unique_ptr<IArray> array)
{
    if (!array || entries_.size() >= index_mask) { return {}; }

    entries_.push_back(Entry{owner, std::move(array)});
    const uint32_t index = static_cast<uint32_t>(entries_.size());
    object_nodes_[owner.object.to_ull()].push_back(index - 1);
    ++live_nodes_;
    return HeapPtr{component_bit | index};
}

} // namespace nw::smalls
