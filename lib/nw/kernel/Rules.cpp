#include "Rules.hpp"

#include "../util/string.hpp"
#include "TwoDACache.hpp"

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    qualifier_ = qualifier_type{};
    selector_ = selector_type{};
    entries_.clear();
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
}

void Rules::add(Modifier mod)
{
    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), mod);
    if (it == std::end(entries_)) {
        entries_.push_back(std::move(mod));
    } else {
        entries_.insert(it, std::move(mod));
    }
}

const TwoDA* Rules::ip_cost_table(size_t table) const
{
    return ip_cost_table_[table];
}

const TwoDA* Rules::ip_param_table(size_t table) const
{
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

int Rules::remove(std::string_view tag)
{
    std::string_view prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }
    if (prefix.back() == '*') {
        prefix = std::string_view{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    auto it = std::remove_if(std::begin(entries_), std::end(entries_),
        [&](const auto& mod) {
            if (starts) {
                if (string::startswith(mod.tagged, prefix)) {
                    ++result;
                    return true;
                }
            } else if (mod.tagged == prefix) {
                ++result;
                return true;
            }
            return false;
        });

    entries_.erase(it, std::end(entries_));

    return result;
}

int Rules::replace(std::string_view tag, ModifierInputs value)
{
    std::string_view prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }

    if (prefix.back() == '*') {
        prefix = std::string_view{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    for (auto& mod : entries_) {
        if (starts) {
            if (string::startswith(mod.tagged, prefix)) {
                mod.value = std::move(value);
                ++result;
            }
        } else if (mod.tagged == prefix) {
            mod.value = std::move(value);
            ++result;
        }
    }
    return result;
}

int Rules::replace(std::string_view tag, const Requirement& req)
{
    std::string_view prefix = tag;
    bool starts = false;
    int result = 0;

    if (prefix.empty()) {
        return result;
    }

    if (prefix.back() == '*') {
        prefix = std::string_view{prefix.data(), prefix.size() - 1};
        if (prefix.empty()) {
            return result;
        }
        starts = true;
    }

    for (auto& mod : entries_) {
        if (starts) {
            if (string::startswith(mod.tagged, prefix)) {
                mod.requirement = req;
                ++result;
            }
        } else if (mod.tagged == prefix) {
            mod.requirement = req;
            ++result;
        }
    }
    return result;
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
