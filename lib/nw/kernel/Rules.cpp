#include "Rules.hpp"

#include "../util/string.hpp"

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
}

void Rules::initialize()
{
    LOG_F(INFO, "kernel: initializing rules system");
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
