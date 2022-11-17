#pragma once

#include "../components/Equips.hpp"
#include "../components/ObjectBase.hpp"
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
using ItemPropFunc = std::function<Effect*(const ItemProperty&, EquipIndex)>;

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

    /// Generates an effect from an item property
    Effect* generate(const ItemProperty& property, EquipIndex index) const;

    /// Removes an effect to an object
    bool remove(ObjectBase* obj, Effect* effect);

    /// Gets stats regarding the effect system
    EffectSystemStats stats() const noexcept;

private:
    absl::flat_hash_map<int32_t, EffectPair> registry_;
    absl::flat_hash_map<int32_t, ItemPropFunc> itemprops_;

    std::deque<Effect> pool_;
    std::stack<uint32_t> free_list_;
};

inline EffectSystem& effects()
{
    auto res = detail::s_services.get_mut<EffectSystem>();
    return res ? *res : *detail::s_services.add<EffectSystem>();
}

} // namespace nw::kernel
