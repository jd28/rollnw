#include "EffectSystem.hpp"

#include <limits>

namespace nw::kernel {

bool EffectSystem::add(EffectType type, EffectFunc apply, EffectFunc remove)
{
    auto [it, added] = registry_.emplace(*type, std::make_pair(std::move(apply), std::move(remove)));
    return added;
}

bool EffectSystem::add(ItemPropertyType type, ItemPropFunc generator)
{
    auto [it, added] = itemprops_.emplace(*type, std::move(generator));
    return added;
}

bool EffectSystem::apply(ObjectBase* obj, Effect* effect)
{
    if (!effect) { return false; }
    auto it = registry_.find(*effect->type);
    if (it == std::end(registry_)) { return false; }
    if (it->second.first && it->second.first(obj, effect)) {
        return true;
    }
    return false;
}

Effect* EffectSystem::create(EffectType type)
{
    EffectID id;
    Effect* effect = nullptr;

    if (free_list_.size()) {
        auto index = free_list_.top();
        free_list_.pop();
        id = pool_[index].id();
        if (id.version != std::numeric_limits<uint32_t>::max()) {
            effect = &pool_[index];
            id.version++;
            effect->type = type;
            effect->set_id(id);
        }
    }

    if (!effect) {
        id.index = uint32_t(pool_.size());
        id.version = 0;
        pool_.emplace_back(type);
        effect = &pool_.back();
        effect->set_id(id);
    }

    return effect;
}

void EffectSystem::clear()
{
    registry_.clear();
    pool_.clear();
    std::stack<uint32_t> s{};
    free_list_.swap(s);
}

void EffectSystem::destroy(Effect* effect)
{
    if (!effect) { return; }
    auto id = effect->id();
    effect->clear();
    free_list_.push(id.index);
}

Effect* EffectSystem::generate(const ItemProperty& property, EquipIndex index) const
{
    auto it = itemprops_.find(property.type);
    if (it == std::end(itemprops_)) { return nullptr; }
    if (it->second) {
        return it->second(property, index);
    }
    return nullptr;
}

bool EffectSystem::remove(ObjectBase* obj, Effect* effect)
{
    if (!effect) { return false; }
    auto it = registry_.find(*effect->type);
    if (it == std::end(registry_)) { return false; }
    if (it->second.second && it->second.second(obj, effect)) {
        return true;
    }
    return false;
}

EffectSystemStats EffectSystem::stats() const noexcept
{
    EffectSystemStats result;
    result.free_list_size = free_list_.size();
    result.pool_size = pool_.size();
    return result;
}

} // namespace nw::kernel
