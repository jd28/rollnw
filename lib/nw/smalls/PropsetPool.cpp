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
    if (!def || !def->decl) {
        return false;
    }
    for (const auto& ann : def->decl->annotations_) {
        if (ann.name.loc.view() == name) {
            return true;
        }
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

bool PropsetPoolManager::is_propset_type(const Runtime& rt, TypeID type_id) const
{
    const Type* type = rt.get_type(type_id);
    if (!type || type->type_kind != TK_struct || !type->type_params[0].is<StructID>()) {
        return false;
    }
    const StructDef* def = rt.type_table_.get(type->type_params[0].as<StructID>());
    return has_annotation(def, "propset");
}

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
    pool.info.field_ids.reserve(def->field_count);
    for (uint32_t i = 0; i < def->field_count; ++i) {
        pool.info.field_ids.push_back(i);
        pool.info.offset_to_field[def->fields[i].offset] = i;
    }

    auto [inserted_it, _] = pools_.emplace(type_id, std::move(pool));
    return &inserted_it->second;
}

PropsetPoolManager::SlotRef PropsetPoolManager::allocate_slot(Pool& pool)
{
    if (!pool.free_slots.empty()) {
        SlotRef slot = pool.free_slots.back();
        pool.free_slots.pop_back();
        return slot;
    }

    Slab slab;
    slab.storage.resize(static_cast<size_t>(slots_per_slab) * pool.info.def->size);
    slab.slots.resize(slots_per_slab);
    // one card bit per slots_per_card slots; covers full slab
    slab.dirty_heap_cards.resize((slots_per_slab + slots_per_card - 1) / slots_per_card, 0);

    pool.slabs.push_back(std::move(slab));
    const uint32_t slab_index = static_cast<uint32_t>(pool.slabs.size() - 1);
    // Slots 1..slots_per_slab-1 are pushed into free_slots in reverse order (LIFO).
    // Slot 0 is returned directly — it is never pushed to avoid a bookkeeping asymmetry
    // where the first alloc from a fresh slab would require an immediate pop-then-return.
    for (uint32_t i = slots_per_slab - 1; i > 0; --i) {
        pool.free_slots.push_back(SlotRef{slab_index, i});
    }
    return SlotRef{slab_index, 0};
}

uint8_t* PropsetPoolManager::slot_ptr(Pool& pool, const SlotRef& slot)
{
    Slab& slab = pool.slabs[slot.slab];
    return slab.storage.data() + static_cast<size_t>(slot.index) * pool.info.def->size;
}

const uint8_t* PropsetPoolManager::slot_ptr(const Pool& pool, const SlotRef& slot) const
{
    const Slab& slab = pool.slabs[slot.slab];
    return slab.storage.data() + static_cast<size_t>(slot.index) * pool.info.def->size;
}

void PropsetPoolManager::mark_slot_dirty(Pool& pool, const SlotRef& slot, uint32_t field_index, bool is_heap_field)
{
    Slab& slab = pool.slabs[slot.slab];
    Slot& slot_data = slab.slots[slot.index];
    if (field_index < 64) {
        slot_data.dirty_bits |= (uint64_t{1} << field_index);
    } else {
        slot_data.dirty_bits = ~uint64_t{0};
    }
    slot_data.aggregate_dirty = true;

    // Only mark the GC card dirty for heap-ref field mutations (write barrier).
    // Scalar field mutations affect replication tracking (dirty_bits/aggregate_dirty)
    // but do not require GC scanning of the card.
    if (is_heap_field) {
        uint32_t card = slot.index / slots_per_card;
        if (card < slab.dirty_heap_cards.size()) {
            slab.dirty_heap_cards[card] = 1;
        }
    }
}

void PropsetPoolManager::bind_heap_owner(Pool& pool, const SlotRef& slot, uint32_t field_index, HeapPtr ptr)
{
    if (ptr.value == 0) {
        return;
    }
    Slab& slab = pool.slabs[slot.slab];
    ObjectHandle owner = slab.slots[slot.index].owner;
    heap_owners_[ptr.value] = HeapOwner{
        .propset_type = pool.info.type_id,
        .object_id = static_cast<uint32_t>(owner.id),
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

void PropsetPoolManager::update_slot_heap_liveness(Runtime& rt, Pool& pool, const SlotRef& slot)
{
    Slab& slab = pool.slabs[slot.slab];
    Slot& slot_data = slab.slots[slot.index];
    uint8_t* base = slot_ptr(pool, slot);
    bool has_live = false;
    for (uint32_t i = 0; i < pool.info.def->heap_ref_count; ++i) {
        auto* ptr = reinterpret_cast<HeapPtr*>(base + pool.info.def->heap_ref_offsets[i]);
        if (ptr->value != 0) {
            has_live = true;
            break;
        }
    }
    slot_data.has_live_heap_refs = has_live;
    if (has_live) {
        uint32_t card = slot.index / slots_per_card;
        if (card < slab.dirty_heap_cards.size()) {
            slab.dirty_heap_cards[card] = 1;
        }
    }
}

void PropsetPoolManager::free_slot(Runtime& rt, Pool& pool, uint32_t object_id, const SlotRef& slot)
{
    Slab& slab = pool.slabs[slot.slab];
    Slot& slot_data = slab.slots[slot.index];
    if (!slot_data.alive) {
        return;
    }

    uint8_t* base = slot_ptr(pool, slot);
    for (uint32_t i = 0; i < pool.info.def->heap_ref_count; ++i) {
        auto* ptr = reinterpret_cast<HeapPtr*>(base + pool.info.def->heap_ref_offsets[i]);
        if (ptr->value != 0) {
            heap_owners_.erase(ptr->value);
            *ptr = HeapPtr{0};
        }
    }

    std::memset(base, 0, pool.info.def->size);
    slot_data = Slot{}; // Reset all slot fields to default
    pool.object_slots.erase(object_id);
    pool.free_slots.push_back(slot);
}

bool PropsetPoolManager::validate_ref(Runtime& rt, const Value& propset_ref, Pool*& out_pool, SlotRef& out_slot, uint32_t& out_object_id)
{
    out_pool = nullptr;
    out_slot = {};
    out_object_id = 0;

    if (!is_propset_type(rt, propset_ref.type_id)) {
        return false;
    }

    Pool* pool = get_pool(propset_ref.type_id);
    if (!pool) {
        return false;
    }

    ObjectHandle obj = propset_ref.data.oval;
    if (!nw::kernel::objects().valid(obj)) {
        return false;
    }

    uint32_t object_id = static_cast<uint32_t>(obj.id);
    auto it = pool->object_slots.find(object_id);
    if (it == pool->object_slots.end()) {
        return false;
    }

    const SlotRef slot = it->second;
    const Slab& slab = pool->slabs[slot.slab];
    const Slot& slot_data = slab.slots[slot.index];
    if (!slot_data.alive || !(slot_data.owner == obj)) {
        return false;
    }

    out_pool = pool;
    out_slot = slot;
    out_object_id = object_id;
    return true;
}

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

    uint32_t object_id = static_cast<uint32_t>(obj.id);
    auto it = pool->object_slots.find(object_id);
    if (it != pool->object_slots.end()) {
        const SlotRef slot = it->second;
        const Slab& slab = pool->slabs[slot.slab];
        const Slot& slot_data = slab.slots[slot.index];
        if (slot_data.alive && slot_data.owner == obj) {
            Value out(propset_type);
            out.storage = ValueStorage::immediate;
            out.data.oval = obj;
            return out;
        }
        free_slot(rt, *pool, object_id, slot);
    }

    SlotRef slot = allocate_slot(*pool);
    Slab& slab = pool->slabs[slot.slab];
    Slot& slot_data = slab.slots[slot.index];

    uint8_t* base = slot_ptr(*pool, slot);
    std::memset(base, 0, pool->info.def->size);
    rt.initialize_zero_defaults(propset_type, base);

    slot_data.alive = true;
    slot_data.owner = obj;
    slot_data.dirty_bits = 0;
    slot_data.aggregate_dirty = false;
    pool->object_slots[object_id] = slot;
    update_slot_heap_liveness(rt, *pool, slot);

    if (pool->info.def->heap_ref_count > 0) {
        for (uint32_t i = 0; i < pool->info.def->heap_ref_count; ++i) {
            uint32_t heap_off = pool->info.def->heap_ref_offsets[i];
            auto* ptr = reinterpret_cast<HeapPtr*>(base + heap_off);
            auto field_it = pool->info.offset_to_field.find(heap_off);
            if (ptr->value != 0 && field_it != pool->info.offset_to_field.end()) {
                bind_heap_owner(*pool, slot, field_it->second, *ptr);
            }
        }
    }

    Value out(propset_type);
    out.storage = ValueStorage::immediate;
    out.data.oval = obj;
    return out;
}

Value PropsetPoolManager::read_field(Runtime& rt, const Value& propset_ref, uint32_t offset, TypeID field_type, bool mark_heap_get_dirty)
{
    Pool* pool = nullptr;
    SlotRef slot{};
    uint32_t object_id = 0;
    if (!validate_ref(rt, propset_ref, pool, slot, object_id)) {
        rt.fail("dangling propset reference");
        return Value{};
    }

    uint8_t* base = slot_ptr(*pool, slot);
    if (rt.type_table_.is_heap_type(field_type)) {
        auto* ptr = reinterpret_cast<HeapPtr*>(base + offset);
        if (ptr->value == 0) {
            rt.initialize_zero_defaults(field_type, reinterpret_cast<uint8_t*>(ptr));
            auto field_it = pool->info.offset_to_field.find(offset);
            if (field_it != pool->info.offset_to_field.end()) {
                bind_heap_owner(*pool, slot, field_it->second, *ptr);
            }
            update_slot_heap_liveness(rt, *pool, slot);
        }
    }
    Value out = read_field_value(rt, base + offset, field_type);

    if (mark_heap_get_dirty && rt.type_table_.is_heap_type(field_type)) {
        auto field_it = pool->info.offset_to_field.find(offset);
        if (field_it != pool->info.offset_to_field.end()) {
            mark_slot_dirty(*pool, slot, field_it->second, /*is_heap_field=*/true);
            if (out.storage == ValueStorage::heap) {
                bind_heap_owner(*pool, slot, field_it->second, out.data.hptr);
            }
            update_slot_heap_liveness(rt, *pool, slot);
        }
    }

    return out;
}

bool PropsetPoolManager::write_field(Runtime& rt, const Value& propset_ref, uint32_t offset, TypeID field_type, const Value& value)
{
    Pool* pool = nullptr;
    SlotRef slot{};
    uint32_t object_id = 0;
    if (!validate_ref(rt, propset_ref, pool, slot, object_id)) {
        rt.fail("dangling propset reference");
        return false;
    }

    uint8_t* base = slot_ptr(*pool, slot);
    HeapPtr old_heap_ptr{};
    const bool is_heap_field = rt.type_table_.is_heap_type(field_type);
    if (is_heap_field) {
        old_heap_ptr = *reinterpret_cast<HeapPtr*>(base + offset);
    }
    write_field_value(rt, base + offset, field_type, value);
    if (is_heap_field) {
        HeapPtr new_heap_ptr = *reinterpret_cast<HeapPtr*>(base + offset);
        if (old_heap_ptr.value != 0 && old_heap_ptr.value != new_heap_ptr.value) {
            unbind_heap_owner(old_heap_ptr);
        }
    }

    auto field_it = pool->info.offset_to_field.find(offset);
    if (field_it != pool->info.offset_to_field.end()) {
        mark_slot_dirty(*pool, slot, field_it->second, is_heap_field);
        if (is_heap_field) {
            HeapPtr ptr = *reinterpret_cast<HeapPtr*>(base + offset);
            bind_heap_owner(*pool, slot, field_it->second, ptr);
        }
    }
    update_slot_heap_liveness(rt, *pool, slot);
    return true;
}

void PropsetPoolManager::prune_invalid_owners(Runtime& rt)
{
    for (auto& [_, pool] : pools_) {
        std::vector<std::pair<uint32_t, SlotRef>> stale;
        stale.reserve(pool.object_slots.size());
        for (const auto& [object_id, slot] : pool.object_slots) {
            const Slab& slab = pool.slabs[slot.slab];
            const Slot& slot_data = slab.slots[slot.index];
            if (!slot_data.alive || !nw::kernel::objects().valid(slot_data.owner)) {
                stale.push_back({object_id, slot});
            }
        }
        for (const auto& [object_id, slot] : stale) {
            free_slot(rt, pool, object_id, slot);
        }
    }
}

void PropsetPoolManager::enumerate_roots(Runtime& rt, GCRootVisitor& visitor, bool young_only)
{
    // Pruning dead owners is O(total live propset slots). Skip it on minor GC
    // (young_only=true) where object death is rare; run only on major GC.
    if (!young_only) {
        prune_invalid_owners(rt);
    }

    for (auto& [_, pool] : pools_) {
        if (!pool.info.def || pool.info.def->heap_ref_count == 0) {
            continue;
        }

        for (Slab& slab : pool.slabs) {
            const uint32_t cards = static_cast<uint32_t>(slab.dirty_heap_cards.size());
            for (uint32_t card = 0; card < cards; ++card) {
                if (!young_only || slab.dirty_heap_cards[card]) {
                    uint32_t start = card * slots_per_card;
                    uint32_t end = std::min(start + slots_per_card, slots_per_slab);
                    for (uint32_t i = start; i < end; ++i) {
                        const Slot& slot_data = slab.slots[i];
                        if (!slot_data.alive || !slot_data.has_live_heap_refs) {
                            continue;
                        }
                        uint8_t* base = slab.storage.data() + static_cast<size_t>(i) * pool.info.def->size;
                        for (uint32_t j = 0; j < pool.info.def->heap_ref_count; ++j) {
                            auto* ptr = reinterpret_cast<HeapPtr*>(base + pool.info.def->heap_ref_offsets[j]);
                            if (ptr->value != 0) {
                                visitor.visit_root(ptr);
                            }
                        }
                    }
                    // Clear card after visiting during minor GC so it only gets re-set
                    // when a heap-ref field is actually mutated, restoring O(mutations) cost.
                    if (young_only) {
                        slab.dirty_heap_cards[card] = 0;
                    }
                }
            }
        }
    }
}

void PropsetPoolManager::mark_heap_mutation(HeapPtr ptr)
{
    if (ptr.value == 0) {
        return;
    }
    auto it = heap_owners_.find(ptr.value);
    if (it == heap_owners_.end()) {
        return;
    }

    auto pool_it = pools_.find(it->second.propset_type);
    if (pool_it == pools_.end()) {
        return;
    }
    Pool& pool = pool_it->second;

    auto slot_it = pool.object_slots.find(it->second.object_id);
    if (slot_it == pool.object_slots.end()) {
        return;
    }
    mark_slot_dirty(pool, slot_it->second, it->second.field_index, /*is_heap_field=*/true);
}

} // namespace nw::smalls
