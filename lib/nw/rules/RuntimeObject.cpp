#include "RuntimeObject.hpp"

#include "../log.hpp"
#include "effects.hpp"

#include <algorithm>

namespace nw {

// == RuntimeObjectPool =======================================================

RuntimeObjectPool::RuntimeObjectPool()
    : unmanaged_array_pool_(4096) // Chunk size: 4096 arrays per chunk
{
    // Initialize system types

    // Effect: dedicated object layout
    size_t effect_size = Effect::total_size();
    pools_[TYPE_EFFECT] = std::make_unique<nw::TypedHandlePool>(effect_size);
    variant_layouts_[TYPE_EFFECT] = VariantLayout{Effect::ints_count, Effect::floats_count, Effect::strings_count};

    // Event: 6 ints, 2 floats, 2 strings
    size_t event_size = VariantObject::total_size(6, 2, 2);
    pools_[TYPE_EVENT] = std::make_unique<nw::TypedHandlePool>(event_size);
    variant_layouts_[TYPE_EVENT] = VariantLayout{6, 2, 2};
}

RuntimeObjectPool::~RuntimeObjectPool() = default;

TypedHandle RuntimeObjectPool::allocate_effect()
{
    TypedHandle h = pools_[TYPE_EFFECT]->allocate();
    h.type = TYPE_EFFECT;

    // Initialize Effect object
    auto* obj = static_cast<Effect*>(pools_[TYPE_EFFECT]->get(h));
    if (obj) {
        new (obj) Effect();
        obj->handle().runtime_handle = h;
        obj->handle().effect = obj;
    }

    return h;
}

TypedHandle RuntimeObjectPool::allocate_event()
{
    TypedHandle h = pools_[TYPE_EVENT]->allocate();
    h.type = TYPE_EVENT;

    // Initialize VariantObject fields
    auto* obj = static_cast<VariantObject*>(pools_[TYPE_EVENT]->get(h));
    if (obj) {
        obj->handle = h;
        obj->num_ints = 6;
        obj->num_floats = 2;
        obj->num_strings = 2;

        // Placement construct strings
        for (uint16_t i = 0; i < 2; ++i) {
            new (obj->strings() + i) String();
        }
    }

    return h;
}

RuntimeObject* RuntimeObjectPool::get(TypedHandle h)
{
    auto* pool = pools_[h.type].get();
    if (!pool) {
        return nullptr;
    }
    return static_cast<RuntimeObject*>(pool->get(h));
}

const RuntimeObject* RuntimeObjectPool::get(TypedHandle h) const
{
    const auto* pool = pools_[h.type].get();
    if (!pool) {
        return nullptr;
    }
    return static_cast<const RuntimeObject*>(pool->get(h));
}

bool RuntimeObjectPool::valid(TypedHandle h) const
{
    const auto* pool = pools_[h.type].get();
    if (!pool) {
        return false;
    }
    return pool->valid(h);
}

void RuntimeObjectPool::destroy(TypedHandle h)
{
    auto* pool = pools_[h.type].get();
    if (!pool) {
        LOG_F(WARNING, "[RuntimeObjectPool] Attempted to destroy handle with invalid type: {}",
            static_cast<int>(h.type));
        return;
    }

    // Get object and destroy strings if it's a variant object
    auto* obj = get(h);
    if (obj && h.type == TYPE_EFFECT) {
        auto* effect = static_cast<Effect*>(obj);
        effect->~Effect();
    } else if (obj && variant_layouts_[h.type].num_strings > 0) {
        auto* variant = static_cast<VariantObject*>(obj);
        for (uint16_t i = 0; i < variant->num_strings; ++i) {
            variant->strings()[i].~String();
        }
    }

    // Destroy the handle in the pool
    pool->destroy(h);
}

uint8_t RuntimeObjectPool::register_variant_type(StringView name, uint16_t num_ints, uint16_t num_floats, uint16_t num_strings)
{
    if (next_user_type_id_ == 0) {
        LOG_F(ERROR, "[RuntimeObjectPool] No more user type slots available (max 128)");
        return TYPE_INVALID;
    }

    uint8_t type_id = next_user_type_id_++;

    // Create pool for this type
    size_t object_size = VariantObject::total_size(num_ints, num_floats, num_strings);
    pools_[type_id] = std::make_unique<nw::TypedHandlePool>(object_size);

    // Store layout metadata
    variant_layouts_[type_id] = VariantLayout{num_ints, num_floats, num_strings};

    LOG_F(INFO, "[RuntimeObjectPool] Registered user variant type '{}' (id={}, ints={}, floats={}, strings={})",
        name, type_id, num_ints, num_floats, num_strings);

    return type_id;
}

TypedHandle RuntimeObjectPool::allocate_variant(uint8_t type_id)
{
    auto* pool = pools_[type_id].get();
    if (!pool) {
        LOG_F(ERROR, "[RuntimeObjectPool] Attempted to allocate unregistered type: {}", type_id);
        return TypedHandle{};
    }

    TypedHandle h = pool->allocate();
    h.type = type_id;

    // Initialize VariantObject fields
    const auto& layout = variant_layouts_[type_id];
    auto* obj = static_cast<VariantObject*>(pool->get(h));
    if (obj) {
        obj->handle = h;
        obj->num_ints = layout.num_ints;
        obj->num_floats = layout.num_floats;
        obj->num_strings = layout.num_strings;

        // Placement construct strings
        for (uint16_t i = 0; i < layout.num_strings; ++i) {
            new (obj->strings() + i) String();
        }
    }

    return h;
}

const VariantLayout* RuntimeObjectPool::get_variant_layout(uint8_t type_id) const
{
    if (!pools_[type_id]) {
        return nullptr;
    }
    return &variant_layouts_[type_id];
}

// == Unmanaged Array Implementation ==========================================

TypedHandle RuntimeObjectPool::allocate_unmanaged_array(smalls::TypeID elem_type, uint32_t initial_capacity)
{
    smalls::IArray* arr = smalls::create_unmanaged_array(elem_type, initial_capacity);

    if (!arr) {
        LOG_F(ERROR, "[RuntimeObjectPool] Unsupported element type for unmanaged array: {}", elem_type.value);
        return TypedHandle{};
    }

    // Store in pool using wrapper
    UnmanagedArrayHandle* wrapper = unmanaged_array_pool_.create();
    if (!wrapper) {
        delete arr;
        LOG_F(ERROR, "[RuntimeObjectPool] Failed to get pool slot for unmanaged array");
        return TypedHandle{};
    }

    wrapper->array = arr;

    // Convert generic Handle to TypedHandle
    TypedHandle th;
    th.id = wrapper->generic_handle.id;
    th.type = TYPE_UNMANAGED_ARRAY;
    th.generation = wrapper->generic_handle.generation;
    return th;
}

smalls::IArray* RuntimeObjectPool::get_unmanaged_array(TypedHandle h)
{
    if (h.type != TYPE_UNMANAGED_ARRAY) {
        return nullptr;
    }

    nw::Handle nh;
    nh.id = h.id;
    nh.generation = h.generation;

    UnmanagedArrayHandle* wrapper = unmanaged_array_pool_.get(nh);
    return wrapper ? wrapper->array : nullptr;
}

void RuntimeObjectPool::destroy_unmanaged_array(TypedHandle h)
{
    if (h.type != TYPE_UNMANAGED_ARRAY) {
        LOG_F(WARNING, "[RuntimeObjectPool] Attempted to destroy non-array handle as unmanaged array");
        return;
    }

    nw::Handle nh;
    nh.id = h.id;
    nh.generation = h.generation;

    UnmanagedArrayHandle* wrapper = unmanaged_array_pool_.get(nh);
    if (wrapper && wrapper->array) {
        delete wrapper->array; // Virtual destructor handles concrete cleanup
        wrapper->array = nullptr;
    }

    unmanaged_array_pool_.destroy(nh);
}

bool RuntimeObjectPool::valid_unmanaged_array(TypedHandle h) const
{
    if (h.type != TYPE_UNMANAGED_ARRAY) {
        return false;
    }

    nw::Handle nh;
    nh.id = h.id;
    nh.generation = h.generation;

    return unmanaged_array_pool_.valid(nh);
}

} // namespace nw
