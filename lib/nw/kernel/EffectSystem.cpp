#include "EffectSystem.hpp"

#include "TwoDACache.hpp"

#include <nlohmann/json.hpp>

#include <limits>

namespace nw::kernel {

const std::type_index EffectSystem::type_index{typeid(EffectSystem)};

EffectSystem::EffectSystem(MemoryResource* allocator)
    : Service(allocator)
    , pool_(1024, allocator)
{
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
    Effect* effect = pool_.allocate();
    effect->type = type;
    return effect;
}

void EffectSystem::destroy(Effect* effect)
{
    if (!effect) { return; }
    pool_.free(effect);
}

void EffectSystem::initialize(ServiceInitTime time)
{
    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_post_load) {
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();

    LOG_F(INFO, "kernel: effect system initializing...");
    LOG_F(INFO, "  ... loading item property cost tables");
    auto costtable = twodas().get("iprp_costtable");
    if (costtable) {
        ip_cost_table_.resize(costtable->rows());
        std::optional<StringView> resref;
        int count = 0;
        for (size_t i = 0; i < costtable->rows(); ++i) {
            if ((resref = costtable->get<StringView>(i, "Name"))) {
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
        std::optional<StringView> resref;
        int count = 0;
        for (size_t i = 0; i < paramtable->rows(); ++i) {
            if ((resref = paramtable->get<StringView>(i, "TableResRef"))) {
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
        std::optional<StringView> temp;
        for (size_t i = 0; i < ipdef->rows(); ++i) {
            ItemPropertyDefinition def;
            if (ipdef->get_to(i, "Name", def.name)) {
                if ((temp = ipdef->get<StringView>(i, "SubTypeResRef"))) {
                    def.subtype = twodas().get(*temp);
                }
                if (auto cost = ipdef->get<int>(i, "CostTableResRef")) {
                    def.cost_table = ip_cost_table_[size_t(*cost)];
                }
                if (auto param = ipdef->get<int>(i, "Param1ResRef")) {
                    def.param_table = ip_param_table_[size_t(*param)];
                }
                ipdef->get_to(i, "Cost", def.cost);
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

    LOG_F(INFO, "  ... loading item property baseitem table");
    itemprop_table_ = twodas().get("itemprops");

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    LOG_F(INFO, "kernel: effect system initialized. ({}ms)",
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
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

const StaticTwoDA* EffectSystem::ip_cost_table(size_t table) const
{
    if (table >= ip_cost_table_.size()) { return nullptr; }
    return ip_cost_table_[table];
}

const ItemPropertyDefinition* EffectSystem::ip_definition(ItemPropertyType type) const
{
    if (type.idx() >= ip_definitions_.size()) { return nullptr; }
    return &ip_definitions_[type.idx()];
}

const Vector<ItemPropertyDefinition>& EffectSystem::ip_definitions() const noexcept
{
    return ip_definitions_;
}

const StaticTwoDA* EffectSystem::ip_param_table(size_t table) const
{
    if (table >= ip_param_table_.size()) { return nullptr; }
    return ip_param_table_[table];
}

const StaticTwoDA* EffectSystem::itemprops() const noexcept
{
    return itemprop_table_;
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

nlohmann::json EffectSystem::stats() const
{
    nlohmann::json j;
    j["effect system"] = {
        {"total_effect_free_list", pool_.free_list_.size()},
        {"total_effects_allocated", pool_.allocated_}};
    return j;
}

} // namespace nw::kernel
