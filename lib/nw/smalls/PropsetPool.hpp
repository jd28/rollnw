#pragma once

#include "ScriptHeap.hpp"
#include "types.hpp"

#include "../objects/ObjectHandle.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace nw::smalls {

struct Runtime;
struct GCRootVisitor;
struct Value;

/// Header stored at the start of every propset entry in the chunked array.
/// Entry layout: [PropsetHeader (24 bytes)] [struct_data (def->size bytes)]
/// The header is 24 bytes, keeping struct_data 8-byte aligned.
struct PropsetHeader {
    ObjectHandle owner;      ///< Owning object handle; zero id = unallocated slot.
    uint64_t dirty_bits = 0; ///< Per-field dirty mask; field >= 64 sets all bits.
    uint8_t flags = 0;       ///< Packed booleans — see HDR_* constants.
    uint8_t _pad[7] = {};    ///< Padding to 24 bytes total.

    static constexpr uint8_t HDR_ALIVE = 0x01;
    static constexpr uint8_t HDR_AGGREGATE_DIRTY = 0x02;
    static constexpr uint8_t HDR_HAS_LIVE_HEAP_REFS = 0x04;
    static constexpr uint8_t HDR_IS_STATIC = 0x08;
    static constexpr uint8_t HDR_HAS_UNMANAGED_ARRAYS = 0x10;

    bool alive() const noexcept { return flags & HDR_ALIVE; }
    bool aggregate_dirty() const noexcept { return flags & HDR_AGGREGATE_DIRTY; }
    bool has_live_heap_refs() const noexcept { return flags & HDR_HAS_LIVE_HEAP_REFS; }
    bool is_static() const noexcept { return flags & HDR_IS_STATIC; }
    bool has_unmanaged_arrays() const noexcept { return flags & HDR_HAS_UNMANAGED_ARRAYS; }
};

static_assert(sizeof(PropsetHeader) == 24, "PropsetHeader must be exactly 24 bytes");
static_assert(alignof(PropsetHeader) == 8, "PropsetHeader must be 8-byte aligned");

class PropsetPoolManager {
public:
    /// Slots per chunk — matches the object manager's ObjectArray chunk size.
    static constexpr uint32_t chunk_size = 2048;

    bool is_propset_type(const Runtime& rt, TypeID type_id) const;
    Value get_or_create(Runtime& rt, TypeID propset_type, ObjectHandle obj);

    void init_object_propsets(Runtime& rt, ObjectHandle obj);
    void free_object_propsets(Runtime& rt, ObjectHandle obj);
    void prime_pools(Runtime& rt);

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
        std::vector<uint32_t> unmanaged_array_offsets;
        ObjectType object_type = ObjectType::invalid; // invalid = unrestricted
    };

    struct Pool {
        TypeInfo info;
        /// Chunks indexed by (object_id / chunk_size).
        /// Each chunk holds chunk_size entries of entry_stride bytes each.
        std::vector<std::unique_ptr<uint8_t[]>> chunks;
        uint32_t entry_stride = 0; ///< sizeof(PropsetHeader) + def->size
    };

    struct HeapOwner {
        TypeID propset_type = invalid_type_id;
        uint32_t object_id = 0;
        uint32_t field_index = 0;
    };

    Pool* ensure_pool(Runtime& rt, TypeID type_id);
    Pool* get_pool(TypeID type_id);
    const Pool* get_pool(TypeID type_id) const;

    /// Returns pointer to entry start (PropsetHeader), or nullptr if chunk not allocated.
    uint8_t* get_entry(Pool& pool, uint32_t object_id);
    /// Returns pointer to entry start, allocating the chunk if needed.
    uint8_t* get_or_alloc_entry(Pool& pool, uint32_t object_id);

    void free_entry(Runtime& rt, Pool& pool, uint32_t object_id, PropsetHeader* hdr, uint8_t* data);
    void mark_entry_dirty(PropsetHeader* hdr, uint32_t field_index, bool is_heap_field = false);
    void update_entry_heap_liveness(Runtime& rt, Pool& pool, PropsetHeader* hdr, uint8_t* data);
    void bind_heap_owner(Pool& pool, uint32_t object_id, uint32_t field_index, HeapPtr ptr);
    void unbind_heap_owner(HeapPtr ptr);
    void prune_invalid_owners(Runtime& rt);

    absl::flat_hash_map<TypeID, Pool> pools_;
    absl::flat_hash_map<uint32_t, HeapOwner> heap_owners_;
    absl::flat_hash_map<uint8_t, std::vector<TypeID>> object_type_propsets_;
};

} // namespace nw::smalls
