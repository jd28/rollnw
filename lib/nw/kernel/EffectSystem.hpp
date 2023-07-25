#pragma once

#include "../objects/Equips.hpp"
#include "../objects/ObjectBase.hpp"
#include "../rules/Effect.hpp"
#include "../rules/items.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <deque>
#include <functional>
#include <stack>
#include <utility>

namespace nw::kernel {

using EffectFunc = std::function<bool(ObjectBase*, const Effect*)>;
using EffectPair = std::pair<EffectFunc, EffectFunc>;
using ItemPropFunc = std::function<Effect*(const ItemProperty&, EquipIndex, BaseItem)>;

struct EffectSystemStats {
    size_t free_list_size = 0;
    size_t pool_size = 0;
};

struct EffectSystem : public Service {
    virtual ~EffectSystem() = default;

    /// Adds an effect type to the registry
    bool add(EffectType type, EffectFunc apply, EffectFunc remove);

    /// Adds an item property type to the registry
    bool add(ItemPropertyType type, ItemPropFunc generator);

    /// Applies an effect to an object
    bool apply(ObjectBase* obj, Effect* effect);

    /// Clears effect registry and all effects
    virtual void clear() override;

    /// Creates an effect
    Effect* create(EffectType type);

    /// Destroys an effect
    void destroy(Effect* effect);

    /// Gets ability effect minimum and maximum
    std::pair<int, int> effect_limits_ability() const noexcept;

    /// Gets armor class effect minimum and maximum
    std::pair<int, int> effect_limits_armor_class() const noexcept;

    /// Gets attack effect minimum and maximum
    std::pair<int, int> effect_limits_attack() const noexcept;

    /// Gets skill effect minimum and maximum
    std::pair<int, int> effect_limits_skill() const noexcept;

    /// Generates an effect from an item property
    Effect* generate(const ItemProperty& property, EquipIndex index, BaseItem baseitem) const;

    /// Initialize effect system
    virtual void initialize() override;

    /// Gets an item property cost table
    const TwoDA* ip_cost_table(size_t table) const;

    /// Gets an item property definition
    const ItemPropertyDefinition* ip_definition(ItemPropertyType type) const;

    /// Gets an item property param table
    const TwoDA* ip_param_table(size_t table) const;

    /// Removes an effect to an object
    bool remove(ObjectBase* obj, Effect* effect);

    /// Sets ability effect minimum and maximum
    void set_effect_limits_ability(int min, int max) noexcept;

    /// Sets armor class effect minimum and maximum
    void set_effect_limits_armor_class(int min, int max) noexcept;

    /// Sets attack effect minimum and maximum
    void set_effect_limits_attack(int min, int max) noexcept;

    /// Sets skill effect minimum and maximum
    void set_effect_limits_skill(int min, int max) noexcept;

    /// Gets stats regarding the effect system
    EffectSystemStats stats() const noexcept;

private:
    absl::flat_hash_map<int32_t, EffectPair> registry_;
    absl::flat_hash_map<int32_t, ItemPropFunc> itemprops_;
    std::vector<ItemPropertyDefinition> ip_definitions_;
    std::vector<const TwoDA*> ip_cost_table_;
    std::vector<const TwoDA*> ip_param_table_;
    std::pair<int, int> ability_effect_limits_{-12, 12};
    std::pair<int, int> ac_effect_limits_{-20, 20};
    std::pair<int, int> attack_effect_limits_{-20, 20};
    std::pair<int, int> effect_limits_skill_{-50, 50};

    std::deque<Effect> pool_;
    std::stack<uint32_t> free_list_;
};

inline EffectSystem& effects()
{
    auto res = services().effects.get();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load effects service");
    }
    return *res;
}

} // namespace nw::kernel
