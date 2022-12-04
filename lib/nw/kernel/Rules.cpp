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
}

void Rules::initialize()
{
    LOG_F(INFO, "kernel: initializing rules system");
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
