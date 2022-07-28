#pragma once

#include "../components/ObjectBase.hpp"
#include "../log.hpp"
#include "../rules/BaseItem.hpp"
#include "../rules/Class.hpp"
#include "../rules/Feat.hpp"
#include "../rules/MasterFeat.hpp"
#include "../rules/Modifier.hpp"
#include "../rules/Race.hpp"
#include "../rules/Skill.hpp"
#include "../rules/Spell.hpp"
#include "../rules/system.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace nw::kernel {

struct Rules : public Service {
    using qualifier_type = std::function<bool(const Qualifier&, const ObjectBase*)>;
    using selector_type = std::function<RuleValue(const Selector&, const ObjectBase*)>;

    virtual ~Rules();

    /// Initializes rules system
    virtual void initialize() override;

    /// Clears rules system of all rules and cached 2da files
    virtual void clear() override;

    /// Adds a modifier to the system
    void add(Modifier mod);

    /**
     * @brief Calculates all modifiers of `type`
     *
     * @tparam T is ``int`` or ``float``
     * @tparam U is some subtype that **must** be convertible to int
     */
    template <typename T, typename U = int>
    T calculate(const ObjectBase* obj, const ModifierType type, U subtype = -1) const;

    /**
     * @brief Calculates a modifier
     *
     * @tparam T is ``int`` or ``float``
     */
    template <typename T>
    T calculate(const ObjectBase* obj, const Modifier& mod) const;

    /// Match
    bool match(const Qualifier& qual, const ObjectBase* obj) const;

    /// Meets requirements
    bool meets_requirement(const Requirement& req, const ObjectBase* obj) const;

    /**
     * @brief Removes modifiers by tag
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @return int number of modifiers affected
     */
    int remove(std::string_view tag);

    /**
     * @brief Replace modifier value
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @param value new value
     * @return int number of modifiers affected
     */
    int replace(std::string_view tag, ModifierVariant value);

    /**
     * @brief Replace modifier requirement
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @param req new requirement
     * @return int number of modifiers affected
     */
    int replace(std::string_view tag, const Requirement& req);

    /// Select
    RuleValue select(const Selector&, const ObjectBase*) const;

    /// Set rules qualifier
    void set_qualifier(qualifier_type match);

    /// Set rules selector
    void set_selector(selector_type selector);

    BaseItemArray baseitems;
    ClassArray classes;
    FeatArray feats;
    RaceArray races;
    SpellArray spells;
    SkillArray skills;
    MasterFeatRegistry master_feats;

private:
    qualifier_type qualifier_;
    selector_type selector_;
    std::vector<Modifier> entries_;
};

template <typename T>
T Rules::calculate(const ObjectBase* obj, const Modifier& mod) const
{
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "only int and float are allowed");

    if (!meets_requirement(mod.requirement, obj)) {
        return {};
    }
    if (mod.value.is<T>()) {
        return mod.value.as<T>();
    } else if (mod.value.is<ModifierFunction>()) {
        auto res = mod.value.as<ModifierFunction>()(obj);
        return res.is<T>() ? res.as<T>() : T{};
    } else {
        LOG_F(ERROR, "invalid modifier or type mismatch");
        return T{};
    }
}

template <typename T, typename U>
T Rules::calculate(const ObjectBase* obj, const ModifierType type, U subtype) const
{
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "only int and float are allowed");

    Modifier temp{type, {}, {}, ModifierSource::unknown, Requirement{}, {}, static_cast<int>(subtype)};
    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), temp);

    T result{};
    while (it != std::end(entries_) && it->type == type && it->subtype == static_cast<int>(subtype)) {
        result += calculate<T>(obj, *it);
        ++it;
    }

    return result;
}

inline Rules& rules()
{
    auto res = detail::s_services.get_mut<Rules>();
    return res ? *res : *detail::s_services.add<Rules>();
}

} // namespace nw::kernel
