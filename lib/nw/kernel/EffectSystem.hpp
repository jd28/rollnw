#pragma once

#include "../components/ObjectBase.hpp"
#include "../rules/Effect.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <deque>
#include <functional>
#include <stack>
#include <utility>

namespace nw::kernel {

using EffectFunc = std::function<bool(ObjectBase*, const Effect*)>;
using EffectPair = std::pair<EffectFunc, EffectFunc>;

struct EffectSystemStats {
    size_t free_list_size = 0;
    size_t pool_size = 0;
};

struct EffectSystem : public Service {
    virtual ~EffectSystem() = default;

    /// Adds an effect type to the registry
    bool add(EffectType type, EffectFunc apply, EffectFunc remove);

    /// Applies an effect to an object
    bool apply(ObjectBase* obj, Effect* effect);

    /// Clears effect registry and all effects
    virtual void clear() override;

    /// Creates an effect
    Effect* create(EffectType type);

    /// Destroys an effect
    void destroy(Effect* effect);

    /// Removes an effect to an object
    bool remove(ObjectBase* obj, Effect* effect);

    /// Gets stats regarding the effect system
    EffectSystemStats stats() const noexcept;

private:
    absl::flat_hash_map<int32_t, EffectPair> registry_;

    std::deque<Effect> pool_;
    std::stack<uint32_t> free_list_;
};

inline EffectSystem& effects()
{
    auto res = detail::s_services.get_mut<EffectSystem>();
    return res ? *res : *detail::s_services.add<EffectSystem>();
}

} // namespace nw::kernel
