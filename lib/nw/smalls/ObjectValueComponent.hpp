#pragma once

#include "Array.hpp"
#include "ScriptHeap.hpp"

#include "../objects/ObjectHandle.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace nw::smalls {

struct Runtime;
struct Value;

/// Identifies the propset field that owns an object-value graph.
struct ObjectValueOwner {
    ObjectHandle object{};
    TypeID propset_type = invalid_type_id;
    uint32_t field_index = 0;

    bool operator==(const ObjectValueOwner&) const noexcept = default;
};

/// Object-lifetime storage for package-defined array/value graphs.
///
/// Component pointers occupy the upper half of the 32-bit HeapPtr value space.
/// ScriptHeap owns the lower 2 GiB, so the two address domains are disjoint.
/// Nodes are never reused during one service generation; removing an object
/// invalidates all of its nodes without creating ABA aliases.
class ObjectValueComponent {
public:
    static constexpr uint32_t component_bit = uint32_t{1} << 31;
    static constexpr uint32_t index_mask = component_bit - 1;

    ObjectValueComponent() = default;
    ObjectValueComponent(const ObjectValueComponent&) = delete;
    ObjectValueComponent& operator=(const ObjectValueComponent&) = delete;

    [[nodiscard]] static bool is_component_ptr(HeapPtr ptr) noexcept
    {
        return (ptr.value & component_bit) != 0;
    }

    [[nodiscard]] HeapPtr create_array(Runtime& runtime, ObjectValueOwner owner,
        TypeID element_type, size_t initial_capacity = 0);
    [[nodiscard]] IArray* get_array(HeapPtr ptr) noexcept;
    [[nodiscard]] const IArray* get_array(HeapPtr ptr) const noexcept;
    [[nodiscard]] const ObjectValueOwner* owner(HeapPtr ptr) const noexcept;

    void release(ObjectHandle object) noexcept;

    [[nodiscard]] size_t node_count() const noexcept { return live_nodes_; }
    [[nodiscard]] size_t retained_bytes() const noexcept;

private:
    struct Entry {
        ObjectValueOwner owner{};
        std::unique_ptr<IArray> array;
    };

    [[nodiscard]] HeapPtr insert(ObjectValueOwner owner, std::unique_ptr<IArray> array);

    std::vector<Entry> entries_;
    absl::flat_hash_map<uint64_t, std::vector<uint32_t>> object_nodes_;
    size_t live_nodes_ = 0;
};

} // namespace nw::smalls
