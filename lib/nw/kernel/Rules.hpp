#pragma once

#include "../log.hpp"
#include "../rules/system.hpp"

#include <flecs/flecs.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace nw::kernel {

struct Rules {
    using qualifier_type = std::function<bool(const Qualifier&, const flecs::entity)>;
    using selector_type = std::function<RuleValue(const Selector&, const flecs::entity)>;

    virtual ~Rules();

    /// Intializes rules system
    virtual bool initialize();

    /// Adds a modifier to the system
    void add(Modifier mod);

    /**
     * @brief Calculates all modifiers of `type`
     *
     * @tparam T is ``int`` or ``float``
     */
    template <typename T>
    T calculate(ModifierType type, flecs::entity ent) const;

    /**
     * @brief Calculates a modifier
     *
     * @tparam T is ``int`` or ``float``
     */
    template <typename T>
    T calculate(const Modifier& mod, const flecs::entity ent) const;

    /// Clears rules system of all rules and cached 2da files
    virtual void clear();

    /// Match
    bool match(const Qualifier& qual, const flecs::entity ent) const;

    /// Meets requirements
    bool meets_requirement(const Requirement& req, const flecs::entity ent) const;

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
    int replace(std::string_view tag, Requirement req);

    /// Select
    RuleValue select(const Selector&, const flecs::entity) const;

    /// Set rules qualifier
    void set_qualifier(qualifier_type match);

    /// Set rules selector
    void set_selector(selector_type selector);

private:
    qualifier_type qualifier_;
    selector_type selector_;
    std::vector<Modifier> entries_;
};

template <typename T>
T Rules::calculate(const Modifier& mod, const flecs::entity ent) const
{
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "only int and float are allowed");

    if (!meets_requirement(mod.requirement, ent)) { return {}; }
    if (mod.value.is<T>()) {
        return mod.value.as<T>();
    } else if (mod.value.is<ModifierFunction>()) {
        auto res = mod.value.as<ModifierFunction>()(ent);
        return res.is<T>() ? res.as<T>() : T{};
    } else {
        LOG_F(ERROR, "invalid modifier or type mismatch");
        return T{};
    }
}

template <typename T>
T Rules::calculate(ModifierType type, flecs::entity ent) const
{
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "only int and float are allowed");

    Modifier temp{type, {}, Requirement{}, {}, {}, ModifierSource::unknown};
    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), temp);
    if (it == std::end(entries_)) { return T{}; }

    T result{};
    for (; it->type == type; ++it) {
        result += calculate<T>(*it, ent);
    }

    return result;
}

} // namespace nw::kernel
