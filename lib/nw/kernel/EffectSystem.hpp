#pragma once

#include "../objects/Equips.hpp"
#include "../objects/ObjectBase.hpp"
#include "../rules/Effect.hpp"
#include "../rules/items.hpp"
#include "../util/FunctionPtr.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <deque>
#include <stack>
#include <utility>

namespace nw::kernel {

using EffectFunc = FunctionPtr<bool(ObjectBase*, const Effect*)>;
using EffectPair = std::pair<EffectFunc, EffectFunc>;
using ItemPropFunc = FunctionPtr<Effect*(const ItemProperty&, EquipIndex, BaseItem)>;

struct EffectLimits {
    std::pair<int, int> ability{-12, 12};
    std::pair<int, int> armor_class{-20, 20};
    std::pair<int, int> attack{-20, 20};
    std::pair<int, int> skill{-50, 50};
};

struct EffectSystemStats {
    size_t free_list_size = 0;
    size_t pool_size = 0;
};

struct EffectSystem : public Service {
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
    virtual void initialize(ServiceInitTime time) override;

    /// Gets an item property cost table
    const TwoDA* ip_cost_table(size_t table) const;

    /// Gets an item property definition
    const ItemPropertyDefinition* ip_definition(ItemPropertyType type) const;

    /// Gets an item property param table
    const TwoDA* ip_param_table(size_t table) const;

    /// Removes an effect to an object
    bool remove(ObjectBase* obj, Effect* effect);

    /// Gets stats regarding the effect system
    EffectSystemStats stats() const noexcept;

    /// Effect related limits
    EffectLimits limits;

private:
    absl::flat_hash_map<int32_t, EffectPair> registry_;
    absl::flat_hash_map<int32_t, ItemPropFunc> itemprops_;
    Vector<ItemPropertyDefinition> ip_definitions_;
    Vector<const TwoDA*> ip_cost_table_;
    Vector<const TwoDA*> ip_param_table_;
    std::deque<Effect> pool_;
    std::stack<uint32_t> free_list_;
};

inline EffectSystem& effects()
{
    auto res = services().get_mut<EffectSystem>();
    if (!res) {
        throw std::runtime_error("kernel: unable to effects service");
    }
    return *res;
}

} // namespace nw::kernel
