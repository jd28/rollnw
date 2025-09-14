#pragma once

#include "../objects/ObjectHandle.hpp"
#include "../util/FixedVector.hpp"
#include "../util/FunctionPtr.hpp"
#include "../util/HandlePool.hpp"
#include "Spell.hpp"
#include "Versus.hpp"
#include "items.hpp"
#include "rule_type.hpp"

namespace nw {

// == Effect ==================================================================
// ============================================================================

DECLARE_RULE_TYPE(EffectType);

struct Effect;

enum struct EffectCategory {
    magical,
    extraordinary,
    supernatural,
    item,
    innate,
};

struct EffectHandle {
    EffectType type = EffectType::invalid();
    int subtype = -1;
    ObjectHandle creator;
    Spell spell_id = Spell::invalid();
    EffectCategory category = EffectCategory::magical;
    Effect* effect = nullptr;

    bool operator==(const EffectHandle&) const = default;
    auto operator<=>(const EffectHandle&) const = default;

    // Pool Handle interface. DO NOT modify
    using size_type = uint32_t;
    size_type index = 0;
    size_type generation = 0;
};

struct Effect {
    Effect(nw::MemoryResource* allocator = nw::kernel::global_allocator());
    Effect(EffectType type_, nw::MemoryResource* allocator = nw::kernel::global_allocator());

    /// Clears the effect such that it's as if default constructed
    void reset();

    /// Gets a floating point value
    float get_float(size_t index) const noexcept;

    /// Gets an integer point value
    int get_int(size_t index) const noexcept;

    /// Gets a string value
    StringView get_string(size_t index) const noexcept;

    /// Gets the effect's handle
    EffectHandle& handle() noexcept;

    /// Gets the effect's handle
    const EffectHandle& handle() const noexcept;

    /// Sets a floating point value
    void set_float(size_t index, float value);

    /// Sets an integer point value
    void set_int(size_t index, int value);

    /// Sets the effect's handle
    void set_handle(EffectHandle hndl);

    /// Sets a string value
    void set_string(size_t index, String value);

    /// Sets the versus value
    void set_versus(Versus vs);

    /// Gets the versus value
    const Versus& versus() const noexcept;

    float duration = 0.0f;
    uint32_t expire_day = 0;
    uint32_t expire_time = 0;

private:
    EffectHandle handle_;

    FixedVector<int> integers_;
    FixedVector<float> floats_;
    FixedVector<String> strings_;
    Versus versus_;
};

// == EffectArray =============================================================
// ============================================================================

struct EffectArray {
    EffectArray(nw::MemoryResource* allocator = nw::kernel::global_allocator());

    using storage = Vector<EffectHandle>;

    using iterator = storage::iterator;
    using const_iterator = storage::const_iterator;

    /// Adds an effect
    bool add(Effect* effect);

    iterator begin();
    const_iterator begin() const;

    iterator end();
    const_iterator end() const;

    /// Removes a range of effects
    void erase(iterator first, iterator last);

    /// Removes an effect
    bool remove(Effect* effect);

    /// Gets the number of applied effects
    size_t size() const noexcept;

private:
    nw::MemoryResource* allocator_ = nullptr;
    storage effects_;
};

// == EffectSystem ============================================================
// ============================================================================

enum struct EquipIndex : uint32_t;
struct StaticTwoDA;

using EffectFunc = FunctionPtr<bool(ObjectBase*, const Effect*)>;
using EffectPair = std::pair<EffectFunc, EffectFunc>;
using ItemPropFunc = FunctionPtr<Effect*(const ItemProperty&, EquipIndex, BaseItem)>;

struct EffectLimits {
    std::pair<int, int> ability{-12, 12};
    std::pair<int, int> armor_class{-20, 20};
    std::pair<int, int> attack{-20, 20};
    std::pair<int, int> saves{-20, 20};
    std::pair<int, int> skill{-50, 50};
};

struct EffectSystem : public kernel::Service {
    const static std::type_index type_index;

    EffectSystem(MemoryResource* scope);
    virtual ~EffectSystem() = default;

    /// Adds an effect type to the registry
    bool add(EffectType type, EffectFunc apply, EffectFunc remove);

    /// Adds an item property type to the registry
    bool add(ItemPropertyType type, ItemPropFunc generator);

    /// Applies an effect to an object
    bool apply(ObjectBase* obj, Effect* effect);

    /// Creates an effect
    Effect* create(EffectType type);

    /// Destroys an effect
    void destroy(Effect* effect);

    /// Generates an effect from an item property
    Effect* generate(const ItemProperty& property, EquipIndex index, BaseItem baseitem) const;

    /// Initialize effect system
    virtual void initialize(kernel::ServiceInitTime time) override;

    /// Gets an item property cost table
    const StaticTwoDA* ip_cost_table(size_t table) const;

    /// Gets an item property definition
    const ItemPropertyDefinition* ip_definition(ItemPropertyType type) const;

    /// Gets all item property definitions
    const Vector<ItemPropertyDefinition>& ip_definitions() const noexcept;

    /// Gets an item property param table
    const StaticTwoDA* ip_param_table(size_t table) const;

    /// Gets the 'itemprops.2da' table;
    const StaticTwoDA* itemprops() const noexcept;

    /// Removes an effect to an object
    bool remove(ObjectBase* obj, Effect* effect);

    /// Gets stats regarding the effect system
    nlohmann::json stats() const override;

    /// Effect related limits
    EffectLimits limits;

private:
    absl::flat_hash_map<int32_t, EffectPair> registry_;
    absl::flat_hash_map<int32_t, ItemPropFunc> itemprops_;
    Vector<ItemPropertyDefinition> ip_definitions_;
    Vector<const StaticTwoDA*> ip_cost_table_;
    Vector<const StaticTwoDA*> ip_param_table_;
    const StaticTwoDA* itemprop_table_;

    HandlePool<EffectHandle, Effect> pool_;
};

} // namespace nw
