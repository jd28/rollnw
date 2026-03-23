#include "PropsetPool.hpp"

#include "Ast.hpp"
#include "VirtualMachine.hpp"
#include "runtime.hpp"

#include "../kernel/Kernel.hpp"
#include "../objects/ObjectManager.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace nw::smalls {

namespace {

constexpr uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

inline uint64_t propset_profile_now_ns() noexcept
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

// == Thread-Local Propset Cache ==============================================
// 16-entry direct-mapped cache for fast propset lookup in hot paths.
// Key: (type_id << 32) | object_id
// Each entry is cache-line sized so a single fetch covers the full entry.

struct alignas(64) PropsetCacheEntry {
    uint64_t key = 0;         // (type_id.value << 32) | object_id
    uint32_t version = 0;     // Object version for stale detection
    uint8_t* entry = nullptr; // Propset data pointer (header at entry[0])
};

// 16-entry cache — comfortable for 3-4 creatures × 3-4 propsets each
thread_local PropsetCacheEntry g_propset_cache[16] = {};
thread_local const void* g_propset_cache_owner = nullptr;

inline void propset_cache_clear_all()
{
    for (auto& cache_entry : g_propset_cache) {
        cache_entry.key = 0;
        cache_entry.version = 0;
        cache_entry.entry = nullptr;
    }
}

// Multiplicative hash for cache indexing — better distribution than XOR for
// small sequential IDs (type_ids and object_ids both tend to be dense).
inline size_t propset_cache_index(TypeID type_id, ObjectHandle obj)
{
    auto h = static_cast<size_t>(type_id.value) * 2654435761u + static_cast<size_t>(obj.id);
    return h & 0xF; // % 16
}

// Check if cache entry is valid for given type/object
inline bool propset_cache_hit(const PropsetCacheEntry& cache_entry, TypeID type_id, ObjectHandle obj)
{
    uint64_t expected_key = (static_cast<uint64_t>(type_id.value) << 32) | static_cast<uint64_t>(obj.id);
    return cache_entry.key == expected_key && cache_entry.version == obj.version;
}

// Update cache with new entry
inline void propset_cache_update(size_t idx, TypeID type_id, ObjectHandle obj, uint8_t* entry)
{
    g_propset_cache[idx].key = (static_cast<uint64_t>(type_id.value) << 32) | static_cast<uint64_t>(obj.id);
    g_propset_cache[idx].version = obj.version;
    g_propset_cache[idx].entry = entry;
}

// Invalidate all cache entries for an object (called on destruction)
inline void propset_cache_invalidate_object(ObjectHandle obj)
{
    for (auto& cache_entry : g_propset_cache) {
        if ((cache_entry.key & 0xFFFFFFFFu) == static_cast<uint64_t>(obj.id)) {
            cache_entry.key = 0;
            cache_entry.entry = nullptr;
        }
    }
}

uint64_t pack_owner(ObjectHandle handle)
{
    return handle.to_ull();
}

ObjectHandle unpack_owner(uint64_t bits)
{
    ObjectHandle handle{};
    handle.id = static_cast<ObjectID>(static_cast<uint32_t>(bits & 0xFFFFFFFFu));
    handle.version = static_cast<uint32_t>((bits >> 32) & 0x00FFFFFFu);
    handle.type = static_cast<ObjectType>((bits >> 56) & 0xFFu);
    return handle;
}

uint32_t unpack_owner_id(uint64_t bits)
{
    return static_cast<uint32_t>(bits & 0xFFFFFFFFu);
}

bool has_annotation(const StructDef* def, StringView name)
{
    if (!def) {
        return false;
    }
    if (name == "propset") {
        return def->is_propset;
    }
    return false;
}

Value read_field_value(Runtime& rt, void* ptr, TypeID type_id)
{
    // Unwrap newtypes to find the underlying storage type while preserving the
    // original type_id so the returned value carries the correct (newtype) type.
    TypeID original_type_id = type_id;
    {
        const Type* t = rt.get_type(type_id);
        while (t && t->type_kind == TK_newtype && t->type_params[0].is<TypeID>()) {
            type_id = t->type_params[0].as<TypeID>();
            t = rt.get_type(type_id);
        }
    }

    if (type_id == rt.int_type()) {
        Value v = Value::make_int(*static_cast<int32_t*>(ptr));
        v.type_id = original_type_id;
        return v;
    }
    if (type_id == rt.float_type()) {
        Value v = Value::make_float(*static_cast<float*>(ptr));
        v.type_id = original_type_id;
        return v;
    }
    if (type_id == rt.bool_type()) {
        Value v = Value::make_bool(*static_cast<bool*>(ptr));
        v.type_id = original_type_id;
        return v;
    }
    if (type_id == rt.string_type()) {
        return Value::make_string(*static_cast<HeapPtr*>(ptr));
    }
    if (rt.is_object_like_type(type_id)) {
        Value v(original_type_id);
        v.storage = ValueStorage::immediate;
        v.data.oval = *static_cast<ObjectHandle*>(ptr);
        return v;
    }

    const Type* type = rt.get_type(type_id);
    if (type && rt.type_table_.is_heap_type(type_id)) {
        return Value::make_heap(*static_cast<HeapPtr*>(ptr), original_type_id);
    }

    return Value{};
}

void write_field_value(Runtime& rt, void* ptr, TypeID type_id, const Value& value)
{
    // Unwrap newtypes to find the underlying storage type for writing raw bytes.
    {
        const Type* t = rt.get_type(type_id);
        while (t && t->type_kind == TK_newtype && t->type_params[0].is<TypeID>()) {
            type_id = t->type_params[0].as<TypeID>();
            t = rt.get_type(type_id);
        }
    }

    if (type_id == rt.int_type()) {
        *static_cast<int32_t*>(ptr) = value.data.ival;
        return;
    }
    if (type_id == rt.float_type()) {
        *static_cast<float*>(ptr) = value.data.fval;
        return;
    }
    if (type_id == rt.bool_type()) {
        *static_cast<bool*>(ptr) = value.data.bval;
        return;
    }
    if (type_id == rt.string_type()) {
        *static_cast<HeapPtr*>(ptr) = value.data.hptr;
        return;
    }
    if (rt.is_object_like_type(type_id)) {
        *static_cast<ObjectHandle*>(ptr) = value.data.oval;
        return;
    }

    *static_cast<HeapPtr*>(ptr) = value.data.hptr;
}

} // namespace

// == Type Queries =============================================================

bool PropsetPoolManager::is_propset_type(const Runtime& rt, TypeID type_id) const
{
    const Type* type = rt.get_type(type_id);
    if (!type || type->type_kind != TK_struct || !type->type_params[0].is<StructID>()) {
        return false;
    }
    const StructDef* def = rt.type_table_.get(type->type_params[0].as<StructID>());
    return has_annotation(def, "propset");
}

// == Pool Management ==========================================================

PropsetPoolManager::Pool* PropsetPoolManager::get_pool(TypeID type_id)
{
    auto it = pools_.find(type_id);
    return it == pools_.end() ? nullptr : &it->second;
}

const PropsetPoolManager::Pool* PropsetPoolManager::get_pool(TypeID type_id) const
{
    auto it = pools_.find(type_id);
    return it == pools_.end() ? nullptr : &it->second;
}

PropsetPoolManager::Pool* PropsetPoolManager::ensure_pool(Runtime& rt, TypeID type_id)
{
    auto it = pools_.find(type_id);
    if (it != pools_.end()) {
        return &it->second;
    }

    const Type* type = rt.get_type(type_id);
    if (!type || type->type_kind != TK_struct || !type->type_params[0].is<StructID>()) {
        return nullptr;
    }
    const StructDef* def = rt.type_table_.get(type->type_params[0].as<StructID>());
    if (!has_annotation(def, "propset")) {
        return nullptr;
    }

    Pool pool;
    pool.info.type_id = type_id;
    pool.info.def = def;
    pool.info.object_type = def->propset_object_type;
    pool.entry_stride = align_up_u32(
        static_cast<uint32_t>(sizeof(PropsetHeader)) + def->size,
        static_cast<uint32_t>(alignof(PropsetHeader)));
    pool.info.field_ids.reserve(def->field_count);
    for (uint32_t i = 0; i < def->field_count; ++i) {
        pool.info.field_ids.push_back(i);
        pool.info.offset_to_field[def->fields[i].offset] = i;
        if (def->fields[i].is_unmanaged_array) {
            pool.info.unmanaged_array_offsets.push_back(def->fields[i].offset);
        }
    }

    auto [inserted_it, _] = pools_.emplace(type_id, std::move(pool));
    if (inserted_it->second.info.object_type != ObjectType::invalid) {
        object_type_propsets_[static_cast<uint8_t>(inserted_it->second.info.object_type)].push_back(type_id);
    }
    return &inserted_it->second;
}

// == Chunk Access =============================================================

uint8_t* PropsetPoolManager::get_entry(Pool& pool, uint32_t object_id)
{
    uint32_t chunk_idx = object_id / chunk_size;
    uint32_t slot_idx = object_id % chunk_size;
    if (chunk_idx >= pool.chunks.size() || !pool.chunks[chunk_idx]) {
        return nullptr;
    }
    return pool.chunks[chunk_idx].get() + static_cast<size_t>(slot_idx) * pool.entry_stride;
}

uint8_t* PropsetPoolManager::get_or_alloc_entry(Pool& pool, uint32_t object_id)
{
    uint32_t chunk_idx = object_id / chunk_size;
    uint32_t slot_idx = object_id % chunk_size;
    if (chunk_idx >= pool.chunks.size()) {
        pool.chunks.resize(chunk_idx + 1);
    }
    if (!pool.chunks[chunk_idx]) {
        size_t chunk_bytes = static_cast<size_t>(chunk_size) * pool.entry_stride;
        pool.chunks[chunk_idx] = std::make_unique<uint8_t[]>(chunk_bytes);
        std::memset(pool.chunks[chunk_idx].get(), 0, chunk_bytes);
    }
    return pool.chunks[chunk_idx].get() + static_cast<size_t>(slot_idx) * pool.entry_stride;
}

// == Entry Lifecycle ==========================================================

void PropsetPoolManager::free_entry(Runtime& rt, Pool& pool, uint32_t /*object_id*/, PropsetHeader* hdr, uint8_t* data)
{
    if (!hdr->alive()) {
        return;
    }

    // Cleanup engine-managed (unmanaged) arrays
    if (hdr->has_unmanaged_arrays()) {
        for (uint32_t offset : pool.info.unmanaged_array_offsets) {
            auto* handle_ptr = reinterpret_cast<TypedHandle*>(data + offset);
            if (handle_ptr->is_valid()) {
                rt.object_pool().destroy_unmanaged_array(*handle_ptr);
                *handle_ptr = TypedHandle{};
            }
        }
    }

    // Release GC-managed heap references
    for (uint32_t i = 0; i < pool.info.def->heap_ref_count; ++i) {
        auto* ptr = reinterpret_cast<HeapPtr*>(data + pool.info.def->heap_ref_offsets[i]);
        if (ptr->value != 0) {
            heap_owners_.erase(ptr->value);
            *ptr = HeapPtr{0};
        }
    }

    std::memset(data, 0, pool.info.def->size);
    hdr->owner_bits = 0;
    hdr->dirty_bits = 0;
    hdr->flags = 0; // Clears HDR_ALIVE and all other flags
}

void PropsetPoolManager::mark_entry_dirty(PropsetHeader* hdr, uint32_t field_index, bool /*is_heap_field*/)
{
    if (field_index < 64) {
        hdr->dirty_bits |= (uint64_t{1} << field_index);
    } else {
        hdr->dirty_bits = ~uint64_t{0};
    }
    hdr->flags |= PropsetHeader::HDR_AGGREGATE_DIRTY;
    hdr->flags &= ~PropsetHeader::HDR_IS_STATIC;
}

void PropsetPoolManager::bind_heap_owner(Pool& pool, uint32_t object_id, uint32_t field_index, HeapPtr ptr)
{
    if (ptr.value == 0) {
        return;
    }
    heap_owners_[ptr.value] = HeapOwner{
        .propset_type = pool.info.type_id,
        .object_id = object_id,
        .field_index = field_index,
    };
}

void PropsetPoolManager::unbind_heap_owner(HeapPtr ptr)
{
    if (ptr.value == 0) {
        return;
    }
    heap_owners_.erase(ptr.value);
}

void PropsetPoolManager::update_entry_heap_liveness(Runtime& /*rt*/, Pool& pool, PropsetHeader* hdr, uint8_t* data)
{
    bool has_live = false;
    for (uint32_t i = 0; i < pool.info.def->heap_ref_count; ++i) {
        auto* ptr = reinterpret_cast<HeapPtr*>(data + pool.info.def->heap_ref_offsets[i]);
        if (ptr->value != 0) {
            has_live = true;
            break;
        }
    }
    if (has_live) {
        hdr->flags |= PropsetHeader::HDR_HAS_LIVE_HEAP_REFS;
    } else {
        hdr->flags &= ~PropsetHeader::HDR_HAS_LIVE_HEAP_REFS;
    }
}

// == get_or_create ============================================================

Value PropsetPoolManager::get_or_create(Runtime& rt, TypeID propset_type, ObjectHandle obj)
{
    struct ProfileScope {
        Runtime& rt;
        bool enabled;
        bool timing;
        uint64_t start;
        ~ProfileScope()
        {
            if (enabled) {
                rt.record_propset_get_or_create(timing ? (propset_profile_now_ns() - start) : 0);
            }
        }
    } profile_scope{rt, rt.is_vm_profile_enabled(), rt.is_vm_profile_timing_enabled(),
        propset_profile_now_ns()};

    if (!nw::kernel::objects().valid(obj)) {
        rt.fail("get_propset: invalid object handle");
        return Value{};
    }

    // Protect against stale pointers when runtimes/pools are recreated between
    // benchmark iterations in the same thread.
    if (ABSL_PREDICT_FALSE(g_propset_cache_owner != this)) {
        propset_cache_clear_all();
        g_propset_cache_owner = this;
    }

    // Fast path: check thread-local cache first (~20-30ns vs ~150ns slow path)
    size_t cache_idx = propset_cache_index(propset_type, obj);
    const PropsetCacheEntry& cache_entry = g_propset_cache[cache_idx];
    if (propset_cache_hit(cache_entry, propset_type, obj)) {
        // Verify the entry is still alive (safety check)
        auto* hdr = reinterpret_cast<PropsetHeader*>(cache_entry.entry);
        if (ABSL_PREDICT_TRUE(hdr->alive())) {
            Value out(propset_type);
            out.storage = ValueStorage::propset;
            out.data.propset_ptr = cache_entry.entry;
            return out;
        }
        // Entry was freed, fall through to slow path
    }

    Pool* pool = ensure_pool(rt, propset_type);
    if (!pool) {
        rt.fail("get_propset: requested type is not a [[propset]] struct");
        return Value{};
    }

    if (pool->info.object_type != ObjectType::invalid && pool->info.object_type != obj.type) {
        rt.fail("get_propset: object type mismatch for type-restricted propset");
        return Value{};
    }

    uint32_t object_id = static_cast<uint32_t>(obj.id);
    uint8_t* entry = get_or_alloc_entry(*pool, object_id);
    auto* hdr = reinterpret_cast<PropsetHeader*>(entry);
    uint8_t* data = entry + sizeof(PropsetHeader);

    if (hdr->alive()) {
        if (unpack_owner(hdr->owner_bits) == obj) {
            // Fast path: entry is already initialized for this object
            // Update cache and return
            propset_cache_update(cache_idx, propset_type, obj, entry);
            Value out(propset_type);
            out.storage = ValueStorage::propset;
            out.data.propset_ptr = entry;
            return out;
        }
        // Same slot ID but different object version — stale entry, reinitialize
        free_entry(rt, *pool, object_id, hdr, data);
    }

    // Initialize a fresh entry
    std::memset(data, 0, pool->info.def->size);
    rt.initialize_zero_defaults(propset_type, data);

    hdr->owner_bits = pack_owner(obj);
    hdr->dirty_bits = 0;
    hdr->flags = PropsetHeader::HDR_ALIVE | PropsetHeader::HDR_IS_STATIC;
    if (!pool->info.unmanaged_array_offsets.empty()) {
        hdr->flags |= PropsetHeader::HDR_HAS_UNMANAGED_ARRAYS;
    }

    // Initialize unmanaged arrays (engine-managed, not GC)
    if (hdr->has_unmanaged_arrays()) {
        for (uint32_t offset : pool->info.unmanaged_array_offsets) {
            auto* handle_ptr = reinterpret_cast<TypedHandle*>(data + offset);
            auto field_it = pool->info.offset_to_field.find(offset);
            if (field_it != pool->info.offset_to_field.end()) {
                uint32_t field_idx = field_it->second;
                TypeID field_type_id = pool->info.def->fields[field_idx].type_id;
                const Type* field_type = rt.get_type(field_type_id);
                if (field_type && field_type->type_kind == TK_array && field_type->type_params[0].is<TypeID>()) {
                    TypeID elem_type = field_type->type_params[0].as<TypeID>();
                    *handle_ptr = rt.object_pool().allocate_unmanaged_array(elem_type, 0);
                }
            }
        }
    }

    // Bind any non-zero heap refs from default initialization
    if (pool->info.def->heap_ref_count > 0) {
        for (uint32_t i = 0; i < pool->info.def->heap_ref_count; ++i) {
            uint32_t heap_off = pool->info.def->heap_ref_offsets[i];
            auto* ptr = reinterpret_cast<HeapPtr*>(data + heap_off);
            auto field_it = pool->info.offset_to_field.find(heap_off);
            if (ptr->value != 0 && field_it != pool->info.offset_to_field.end()) {
                bind_heap_owner(*pool, object_id, field_it->second, *ptr);
                if (auto* gc = rt.gc()) {
                    gc->write_barrier_root(*ptr);
                }
            }
        }
    }

    update_entry_heap_liveness(rt, *pool, hdr, data);

    // Update cache with newly created entry
    propset_cache_update(cache_idx, propset_type, obj, entry);

    Value out(propset_type);
    out.storage = ValueStorage::propset;
    out.data.propset_ptr = entry;
    return out;
}

// == read_field ===============================================================

Value PropsetPoolManager::read_field(Runtime& rt, const Value& propset_ref, uint32_t offset, TypeID field_type, bool mark_heap_get_dirty)
{
    uint8_t* entry = propset_ref.data.propset_ptr;
    auto* hdr = reinterpret_cast<PropsetHeader*>(entry);
    uint8_t* data = entry + sizeof(PropsetHeader);

    if (!hdr->alive()) {
        rt.fail("dangling propset reference");
        return Value{};
    }

    // Fast path: non-heap scalar field with no unmanaged arrays.
    // Dirty marking on read only applies to heap fields, so we can return directly.
    if (!rt.type_table_.is_heap_type(field_type) && !hdr->has_unmanaged_arrays()) {
        return read_field_value(rt, data + offset, field_type);
    }

    // Slow path: heap field or entry contains unmanaged arrays
    Pool* pool = get_pool(propset_ref.type_id);
    if (!pool) {
        rt.fail("dangling propset reference");
        return Value{};
    }

    auto field_it = pool->info.offset_to_field.find(offset);

    // Check for unmanaged array field
    if (hdr->has_unmanaged_arrays() && field_it != pool->info.offset_to_field.end()
        && pool->info.def->fields[field_it->second].is_unmanaged_array) {
        auto* handle_ptr = reinterpret_cast<TypedHandle*>(data + offset);
        if (!handle_ptr->is_valid()) {
            LOG_F(WARNING, "[PropsetPool] Unmanaged array handle not initialized at offset {}", offset);
            return Value{};
        }
        Value out = Value::make_unmanaged_array(*handle_ptr, field_type);
        if (mark_heap_get_dirty) {
            mark_entry_dirty(hdr, field_it->second, /*is_heap_field=*/false);
        }
        return out;
    }

    // TODO(v2-cleanup): Heap-typed propset fields are rejected by the TypeResolver in v1.
    // This branch handles any future relaxation of that constraint.
    if (rt.type_table_.is_heap_type(field_type)) {
        auto* ptr = reinterpret_cast<HeapPtr*>(data + offset);

        const auto ensure_initialized = [&]() {
            if (ptr->value == 0) {
                rt.initialize_zero_defaults(field_type, reinterpret_cast<uint8_t*>(ptr));
                if (field_it != pool->info.offset_to_field.end()) {
                    uint32_t object_id = unpack_owner_id(hdr->owner_bits);
                    bind_heap_owner(*pool, object_id, field_it->second, *ptr);
                    if (auto* gc = rt.gc()) {
                        gc->write_barrier_root(*ptr);
                    }
                }
                update_entry_heap_liveness(rt, *pool, hdr, data);
            }
        };

        if (ptr->value != 0) {
            bool valid = false;
            if (auto* header = rt.heap_.try_get_header(*ptr)) {
                valid = (header->type_id == field_type);
            }
            if (!valid) {
                unbind_heap_owner(*ptr);
                *ptr = HeapPtr{0};
            }
        }

        ensure_initialized();
    }

    Value out = read_field_value(rt, data + offset, field_type);

    if (mark_heap_get_dirty && rt.type_table_.is_heap_type(field_type)) {
        if (field_it != pool->info.offset_to_field.end()) {
            mark_entry_dirty(hdr, field_it->second, /*is_heap_field=*/true);
            if (out.storage == ValueStorage::heap) {
                uint32_t object_id = unpack_owner_id(hdr->owner_bits);
                bind_heap_owner(*pool, object_id, field_it->second, out.data.hptr);
            }
            update_entry_heap_liveness(rt, *pool, hdr, data);
        }
    }

    return out;
}

// == write_field ==============================================================

bool PropsetPoolManager::write_field(Runtime& rt, const Value& propset_ref, uint32_t offset, TypeID field_type, const Value& value)
{
    struct ProfileScope {
        Runtime& rt;
        bool enabled;
        bool timing;
        uint64_t start;
        ~ProfileScope()
        {
            if (enabled) {
                rt.record_propset_write_field(timing ? (propset_profile_now_ns() - start) : 0);
            }
        }
    } profile_scope{rt, rt.is_vm_profile_enabled(), rt.is_vm_profile_timing_enabled(),
        propset_profile_now_ns()};

    uint8_t* entry = propset_ref.data.propset_ptr;
    if (rt.is_vm_profile_enabled()) {
        rt.record_propset_write_field_site(propset_ref.type_id, offset);
    }
    auto* hdr = reinterpret_cast<PropsetHeader*>(entry);
    uint8_t* data = entry + sizeof(PropsetHeader);

    if (!hdr->alive()) {
        rt.fail("dangling propset reference");
        return false;
    }

    Pool* pool = get_pool(propset_ref.type_id);
    if (!pool) {
        rt.fail("dangling propset reference");
        return false;
    }

    auto field_it = pool->info.offset_to_field.find(offset);

    // Reject assignment to unmanaged array fields
    if (field_it != pool->info.offset_to_field.end()
        && pool->info.def->fields[field_it->second].is_unmanaged_array) {
        rt.fail("cannot assign to propset array field (use .push(), .clear(), etc. instead)");
        return false;
    }

    const bool is_heap_field = rt.type_table_.is_heap_type(field_type);
    HeapPtr old_heap_ptr{};
    if (is_heap_field) {
        old_heap_ptr = *reinterpret_cast<HeapPtr*>(data + offset);
    }

    write_field_value(rt, data + offset, field_type, value);

    if (is_heap_field) {
        HeapPtr new_heap_ptr = *reinterpret_cast<HeapPtr*>(data + offset);
        if (old_heap_ptr.value != 0 && old_heap_ptr.value != new_heap_ptr.value) {
            unbind_heap_owner(old_heap_ptr);
        }
    }

    if (field_it != pool->info.offset_to_field.end()) {
        mark_entry_dirty(hdr, field_it->second, is_heap_field);
        if (is_heap_field) {
            HeapPtr ptr = *reinterpret_cast<HeapPtr*>(data + offset);
            uint32_t object_id = unpack_owner_id(hdr->owner_bits);
            bind_heap_owner(*pool, object_id, field_it->second, ptr);
            if (auto* gc = rt.gc()) {
                gc->write_barrier_root(ptr);
            }
        }
    }

    update_entry_heap_liveness(rt, *pool, hdr, data);
    return true;
}

// == Object Lifecycle =========================================================

void PropsetPoolManager::init_object_propsets(Runtime& rt, ObjectHandle obj)
{
    if (obj.type == ObjectType::invalid) { return; }

    auto it = object_type_propsets_.find(static_cast<uint8_t>(obj.type));
    if (it == object_type_propsets_.end()) { return; }

    for (TypeID type_id : it->second) {
        get_or_create(rt, type_id, obj);
    }
}

void PropsetPoolManager::free_object_propsets(Runtime& rt, ObjectHandle obj)
{
    // Invalidate cache entries for this object before freeing
    propset_cache_invalidate_object(obj);

    uint32_t object_id = static_cast<uint32_t>(obj.id);
    for (auto& [_, pool] : pools_) {
        uint8_t* entry = get_entry(pool, object_id);
        if (!entry) { continue; }
        auto* hdr = reinterpret_cast<PropsetHeader*>(entry);
        if (!hdr->alive() || unpack_owner(hdr->owner_bits) != obj) { continue; }
        uint8_t* data = entry + sizeof(PropsetHeader);
        free_entry(rt, pool, object_id, hdr, data);
    }
}

void PropsetPoolManager::prime_pools(Runtime& rt)
{
    const auto& types = rt.type_table_.types_;
    for (size_t i = 0; i < types.size(); ++i) {
        if (types[i].type_kind == TK_struct) {
            ensure_pool(rt, TypeID{static_cast<int32_t>(i + 1)});
        }
    }
}

// == Heap Mutation Notification ===============================================

void PropsetPoolManager::mark_heap_mutation(HeapPtr ptr)
{
    if (ptr.value == 0) {
        return;
    }
    auto it = heap_owners_.find(ptr.value);
    if (it == heap_owners_.end()) {
        return;
    }

    Pool* pool = get_pool(it->second.propset_type);
    if (!pool) {
        return;
    }

    uint8_t* entry = get_entry(*pool, it->second.object_id);
    if (!entry) {
        return;
    }

    auto* hdr = reinterpret_cast<PropsetHeader*>(entry);
    mark_entry_dirty(hdr, it->second.field_index, /*is_heap_field=*/true);
}

// == Pruning ==================================================================

void PropsetPoolManager::prune_invalid_owners(Runtime& rt)
{
    for (auto& [_, pool] : pools_) {
        for (auto& chunk_ptr : pool.chunks) {
            if (!chunk_ptr) { continue; }
            uint8_t* chunk = chunk_ptr.get();
            for (uint32_t i = 0; i < chunk_size; ++i) {
                uint8_t* entry = chunk + static_cast<size_t>(i) * pool.entry_stride;
                auto* hdr = reinterpret_cast<PropsetHeader*>(entry);
                if (!hdr->alive()) { continue; }
                ObjectHandle owner = unpack_owner(hdr->owner_bits);
                if (!nw::kernel::objects().valid(owner)) {
                    uint32_t object_id = unpack_owner_id(hdr->owner_bits);
                    uint8_t* data = entry + sizeof(PropsetHeader);
                    free_entry(rt, pool, object_id, hdr, data);
                }
            }
        }
    }
}

} // namespace nw::smalls
