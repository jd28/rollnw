#include "PropsetPool.hpp"

#include "Ast.hpp"
#include "VirtualMachine.hpp"
#include "runtime.hpp"

#include "../kernel/Kernel.hpp"
#include "../objects/ObjectManager.hpp"

#include <algorithm>
#include <cstring>

namespace nw::smalls {

namespace {

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
    if (type_id == rt.int_type()) {
        return Value::make_int(*static_cast<int32_t*>(ptr));
    }
    if (type_id == rt.float_type()) {
        return Value::make_float(*static_cast<float*>(ptr));
    }
    if (type_id == rt.bool_type()) {
        return Value::make_bool(*static_cast<bool*>(ptr));
    }
    if (type_id == rt.string_type()) {
        return Value::make_string(*static_cast<HeapPtr*>(ptr));
    }
    if (rt.is_object_like_type(type_id)) {
        Value v(type_id);
        v.storage = ValueStorage::immediate;
        v.data.oval = *static_cast<ObjectHandle*>(ptr);
        return v;
    }

    const Type* type = rt.get_type(type_id);
    if (type && rt.type_table_.is_heap_type(type_id)) {
        return Value::make_heap(*static_cast<HeapPtr*>(ptr), type_id);
    }

    return Value{};
}

void write_field_value(Runtime& rt, void* ptr, TypeID type_id, const Value& value)
{
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
    pool.entry_stride = static_cast<uint32_t>(sizeof(PropsetHeader)) + def->size;
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
    hdr->owner = ObjectHandle{};
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
    if (!nw::kernel::objects().valid(obj)) {
        rt.fail("get_propset: invalid object handle");
        return Value{};
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
        if (hdr->owner == obj) {
            // Fast path: entry is already initialized for this object
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

    hdr->owner = obj;
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
                    uint32_t object_id = static_cast<uint32_t>(hdr->owner.id);
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
                uint32_t object_id = static_cast<uint32_t>(hdr->owner.id);
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
    uint8_t* entry = propset_ref.data.propset_ptr;
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
            uint32_t object_id = static_cast<uint32_t>(hdr->owner.id);
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
    uint32_t object_id = static_cast<uint32_t>(obj.id);
    for (auto& [_, pool] : pools_) {
        uint8_t* entry = get_entry(pool, object_id);
        if (!entry) { continue; }
        auto* hdr = reinterpret_cast<PropsetHeader*>(entry);
        if (!hdr->alive() || hdr->owner != obj) { continue; }
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
                if (!nw::kernel::objects().valid(hdr->owner)) {
                    uint32_t object_id = static_cast<uint32_t>(hdr->owner.id);
                    uint8_t* data = entry + sizeof(PropsetHeader);
                    free_entry(rt, pool, object_id, hdr, data);
                }
            }
        }
    }
}

} // namespace nw::smalls
