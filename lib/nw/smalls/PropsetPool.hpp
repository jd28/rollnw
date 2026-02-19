#pragma once

#include "ScriptHeap.hpp"
#include "types.hpp"

#include "../objects/ObjectHandle.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <vector>

namespace nw::smalls {

struct Runtime;
struct GCRootVisitor;
struct Value;

class PropsetPoolManager {
public:
    static constexpr uint32_t slots_per_slab = 256;
    static constexpr uint32_t slots_per_card = 64;

    bool is_propset_type(const Runtime& rt, TypeID type_id) const;
    Value get_or_create(Runtime& rt, TypeID propset_type, ObjectHandle obj);

    Value read_field(Runtime& rt, const Value& propset_ref, uint32_t offset, TypeID field_type, bool mark_heap_get_dirty);
    bool write_field(Runtime& rt, const Value& propset_ref, uint32_t offset, TypeID field_type, const Value& value);

    void mark_heap_mutation(HeapPtr ptr);

private:
    struct TypeInfo {
        TypeID type_id = invalid_type_id;
        const StructDef* def = nullptr;
        uint32_t schema_version = 1;
        std::vector<uint32_t> field_ids;
        absl::flat_hash_map<uint32_t, uint32_t> offset_to_field;
        std::vector<uint32_t> unmanaged_array_offsets; // Byte offsets of unmanaged array fields
    };

    struct SlotRef {
        uint32_t slab = 0;
        uint32_t index = 0;
        bool operator==(const SlotRef& other) const = default;
    };

    struct Slot {
        bool alive = false;
        bool aggregate_dirty = false;
        bool has_live_heap_refs = false;
        bool is_static = false;            // NEW: Skip during root enumeration if true and not dirty
        bool has_unmanaged_arrays = false; // True if slot contains unmanaged array handles
        uint64_t dirty_bits = 0;
        ObjectHandle owner;
    };

    struct Slab {
        std::vector<uint8_t> storage;
        std::vector<Slot> slots;
        std::vector<uint8_t> dirty_heap_cards;
        std::vector<uint16_t> dirty_heap_card_indices;
    };

    struct Pool {
        TypeInfo info;
        std::vector<Slab> slabs;
        std::vector<SlotRef> free_slots;
        absl::flat_hash_map<uint32_t, SlotRef> object_slots;
    };

    struct HeapOwner {
        TypeID propset_type = invalid_type_id;
        uint32_t object_id = 0;
        uint32_t field_index = 0;
    };

    Pool* ensure_pool(Runtime& rt, TypeID type_id);
    Pool* get_pool(TypeID type_id);
    const Pool* get_pool(TypeID type_id) const;

    SlotRef allocate_slot(Pool& pool);
    void free_slot(Runtime& rt, Pool& pool, uint32_t object_id, const SlotRef& slot);
    uint8_t* slot_ptr(Pool& pool, const SlotRef& slot);
    const uint8_t* slot_ptr(const Pool& pool, const SlotRef& slot) const;

    bool validate_ref(Runtime& rt, const Value& propset_ref, Pool*& out_pool, SlotRef& out_slot, uint32_t& out_object_id);
    void mark_slot_dirty(Pool& pool, const SlotRef& slot, uint32_t field_index, bool is_heap_field = false);
    void update_slot_heap_liveness(Runtime& rt, Pool& pool, const SlotRef& slot);
    void bind_heap_owner(Pool& pool, const SlotRef& slot, uint32_t field_index, HeapPtr ptr);
    void unbind_heap_owner(HeapPtr ptr);
    void prune_invalid_owners(Runtime& rt);

    absl::flat_hash_map<TypeID, Pool> pools_;
    absl::flat_hash_map<uint32_t, HeapOwner> heap_owners_;
};

} // namespace nw::smalls
