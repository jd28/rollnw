#include "EffectSystem.hpp"

#include "TwoDACache.hpp"

#include <limits>

namespace nw::kernel {

// == Effect Limits ===========================================================
// ============================================================================

std::pair<int, int> EffectSystem::effect_limits_ability() const noexcept
{
    return ability_effect_limits_;
}

std::pair<int, int> EffectSystem::effect_limits_armor_class() const noexcept
{
    return ac_effect_limits_;
}

std::pair<int, int> EffectSystem::effect_limits_attack() const noexcept
{
    return attack_effect_limits_;
}

std::pair<int, int> EffectSystem::effect_limits_skill() const noexcept
{
    return effect_limits_skill_;
}

void EffectSystem::set_effect_limits_ability(int min, int max) noexcept
{
    ability_effect_limits_ = std::make_pair(min, max);
}

void EffectSystem::set_effect_limits_armor_class(int min, int max) noexcept
{
    ac_effect_limits_ = std::make_pair(min, max);
}

void EffectSystem::set_effect_limits_attack(int min, int max) noexcept
{
    attack_effect_limits_ = std::make_pair(min, max);
}

void EffectSystem::set_effect_limits_skill(int min, int max) noexcept
{
    effect_limits_skill_ = std::make_pair(min, max);
}

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
    itemprops_.clear();
    ip_cost_table_.clear();
    ip_param_table_.clear();
}

void EffectSystem::destroy(Effect* effect)
{
    if (!effect) { return; }
    auto id = effect->id();
    effect->clear();
    free_list_.push(id.index);
}

void EffectSystem::initialize()
{
    LOG_F(INFO, "  ... loading item property cost tables");
    auto costtable = twodas().get("iprp_costtable");
    if (costtable) {
        ip_cost_table_.resize(costtable->rows());
        std::optional<std::string_view> resref;
        int count = 0;
        for (size_t i = 0; i < costtable->rows(); ++i) {
            if ((resref = costtable->get<std::string_view>(i, "Name"))) {
                auto tda = twodas().get(*resref);
                if (!tda) {
                    LOG_F(WARNING, "  ... failed to load cost table {}", *resref);
                    ip_cost_table_[i] = nullptr;
                } else {
                    ip_cost_table_[i] = tda;
                    ++count;
                }
            }
        }
        LOG_F(INFO, "  ... loaded {} item property cost tables", count);
    } else {
        LOG_F(ERROR, "  ... failed to load item property cost tables");
    }

    LOG_F(INFO, "  ... loading item property param tables");
    auto paramtable = twodas().get("iprp_paramtable");
    if (paramtable) {
        ip_param_table_.resize(paramtable->rows());
        std::optional<std::string_view> resref;
        int count = 0;
        for (size_t i = 0; i < paramtable->rows(); ++i) {
            if ((resref = paramtable->get<std::string_view>(i, "TableResRef"))) {
                auto tda = twodas().get(*resref);
                if (!tda) {
                    LOG_F(WARNING, "  ... failed to load param table {}", *resref);
                    ip_param_table_[i] = nullptr;
                } else {
                    ip_param_table_[i] = tda;
                    ++count;
                }
            }
        }
        LOG_F(INFO, "  ... loaded {} item property param tables", count);
    } else {
        LOG_F(ERROR, "Failed to load item property param tables");
    }

    LOG_F(INFO, "  ... loading item property definitions");
    auto ipdef = twodas().get("itempropdef");
    if (ipdef) {
        int count = 0;
        std::optional<std::string_view> temp;
        for (size_t i = 0; i < ipdef->rows(); ++i) {
            ItemPropertyDefinition def;
            if (ipdef->get_to(i, "Name", def.name)) {
                if ((temp = ipdef->get<std::string_view>(i, "SubTypeResRef"))) {
                    def.subtype = twodas().get(*temp);
                }
                if (auto cost = ipdef->get<int>(i, "CostTableResRef")) {
                    def.cost_table = ip_cost_table_[size_t(*cost)];
                }
                if (auto param = ipdef->get<int>(i, "Param1ResRef")) {
                    def.param_table = ip_param_table_[size_t(*param)];
                }
                ipdef->get_to(i, "GameStrRef", def.game_string);
                ipdef->get_to(i, "Description", def.description);
                ++count;
            }
            ip_definitions_.push_back(def);
        }
        LOG_F(INFO, "  ... loaded {} item property definitions", count);
    } else {
        LOG_F(ERROR, "Failed to load item property definitions");
    }
}

Effect* EffectSystem::generate(const ItemProperty& property, EquipIndex index, BaseItem baseitem) const
{
    auto it = itemprops_.find(property.type);
    if (it == std::end(itemprops_)) { return nullptr; }
    if (it->second) {
        return it->second(property, index, baseitem);
    }
    return nullptr;
}

const TwoDA* EffectSystem::ip_cost_table(size_t table) const
{
    if (table >= ip_cost_table_.size()) { return nullptr; }
    return ip_cost_table_[table];
}

const ItemPropertyDefinition* EffectSystem::ip_definition(ItemPropertyType type) const
{
    if (type.idx() >= ip_definitions_.size()) { return nullptr; }
    return &ip_definitions_[type.idx()];
}

const TwoDA* EffectSystem::ip_param_table(size_t table) const
{
    if (table >= ip_param_table_.size()) { return nullptr; }
    return ip_param_table_[table];
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
