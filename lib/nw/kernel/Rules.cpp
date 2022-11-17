#include "Rules.hpp"

#include "../util/string.hpp"
#include "TwoDACache.hpp"

#include <cstddef>

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    qualifier_ = qualifier_type{};
    selector_ = selector_type{};
    modifiers.clear();
    baseitems.entries.clear();
    classes.entries.clear();
    feats.entries.clear();
    races.entries.clear();
    spells.entries.clear();
    skills.entries.clear();
    master_feats.clear();
    ip_cost_table_.clear();
    ip_param_table_.clear();
}

void Rules::initialize()
{
    LOG_F(INFO, "kernel: initializing rules system");

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

const TwoDA* Rules::ip_cost_table(size_t table) const
{
    if (table >= ip_cost_table_.size()) { return nullptr; }
    return ip_cost_table_[table];
}

const ItemPropertyDefinition* Rules::ip_definition(ItemPropertyType type) const
{
    if (type.idx() >= ip_definitions_.size()) { return nullptr; }
    return &ip_definitions_[type.idx()];
}

const TwoDA* Rules::ip_param_table(size_t table) const
{
    if (table >= ip_param_table_.size()) { return nullptr; }
    return ip_param_table_[table];
}

bool Rules::match(const Qualifier& qual, const ObjectBase* obj) const
{
    if (!qualifier_) {
        return true;
    }
    return qualifier_(qual, obj);
}

bool Rules::meets_requirement(const Requirement& req, const ObjectBase* obj) const
{
    for (const auto& q : req.qualifiers) {
        if (req.conjunction) {
            if (!match(q, obj)) {
                return false;
            }
        } else if (match(q, obj)) {
            return true;
        }
    }
    return true;
}

RuleValue Rules::select(const Selector& selector, const ObjectBase* obj) const
{
    if (!selector_) {
        LOG_F(ERROR, "rules: no selector set");
        return {};
    }

    return selector_(selector, obj);
}

void Rules::set_qualifier(qualifier_type match)
{
    qualifier_ = std::move(match);
}

void Rules::set_selector(selector_type selector)
{
    selector_ = std::move(selector);
}

} // namespace nw::kernel
