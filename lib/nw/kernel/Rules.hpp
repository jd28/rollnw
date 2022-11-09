#pragma once

#include "../components/ObjectBase.hpp"
#include "../formats/TwoDA.hpp"
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
     * @tparam Callback Modifier callback function
     */
    template <typename Callback>
    bool calculate(const ObjectBase* obj, const ModifierType type, Callback cb,
        const ObjectBase* versus = nullptr) const;

    /**
     * @brief Calculates all modifiers of `type`
     * @tparam U is some rule subtype
     * @tparam Callback Modifier callback function
     */
    template <typename U, typename Callback>
    bool calculate(const ObjectBase* obj, const ModifierType type, U subtype, Callback cb,
        const ObjectBase* versus = nullptr) const;

    /**
     * @brief Calculates a modifier
     * @tparam Callback Modifier callback function
     */
    template <typename Callback>
    bool calculate(const ObjectBase* obj, const Modifier& mod, Callback cb,
        const ObjectBase* versus = nullptr) const;

    /// Gets an item property cost table
    const TwoDA* ip_cost_table(size_t table) const;

    /// Gets an item property param table
    const TwoDA* ip_param_table(size_t table) const;

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
    int replace(std::string_view tag, ModifierInputs value);

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
    std::vector<const TwoDA*> ip_cost_table_;
    std::vector<const TwoDA*> ip_param_table_;
};

namespace detail {

template <typename T>
constexpr bool is_valid()
{
    return std::is_integral_v<T> || std::is_floating_point_v<T>;
}

template <typename T>
struct function_traits : public function_traits<decltype(&T::operator())> {
};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const> {
    using result_type = ReturnType;
    using tuple_type = std::tuple<Args...>;
    static constexpr auto arity = sizeof...(Args);
    static constexpr bool validate_args()
    {
        return (is_valid<Args>() && ...);
    }
};

template <typename R, typename... Args>
struct function_traits<R (&)(Args...)> {
    using result_type = R;
    using tuple_type = std::tuple<Args...>;
    static constexpr auto arity = sizeof...(Args);
    static constexpr bool validate_args()
    {
        return (is_valid<Args>() && ...);
    }
};

template <typename T>
bool calc_mod_input(T& out, const ObjectBase* obj, const ObjectBase* versus,
    const Modifier& mod, const ModifierVariant& in)
{
    if (in.is<T>()) {
        out = in.as<T>();
    } else if (in.is<ModifierFunction>()) {
        auto res = in.as<ModifierFunction>()(obj);
        if (res.is<T>()) {
            out = res.as<T>();
        } else {
            LOG_F(ERROR, "invalid modifier or type mismatch");
            return false;
        }
    } else if (in.is<ModifierSubFunction>()) {
        auto res = in.as<ModifierSubFunction>()(obj, mod.subtype);
        if (res.is<T>()) {
            out = res.as<T>();
        } else {
            LOG_F(ERROR, "invalid modifier or type mismatch");
            return false;
        }
    } else if (in.is<ModifierVsFunction>()) {
        auto res = in.as<ModifierVsFunction>()(obj, versus);
        if (res.is<T>()) {
            out = res.as<T>();
        } else {
            LOG_F(ERROR, "invalid modifier or type mismatch");
            return false;
        }
    } else if (in.is<ModifierSubVsFunction>()) {
        auto res = in.as<ModifierSubVsFunction>()(obj, versus, mod.subtype);
        if (res.is<T>()) {
            out = res.as<T>();
        } else {
            LOG_F(ERROR, "invalid modifier or type mismatch");
            return false;
        }
    } else {
        LOG_F(ERROR, "invalid modifier or type mismatch");
        return false;
    }
    return true;
}

template <typename TupleT, std::size_t... Is>
bool calc_mod_inputs(TupleT& tp, const ObjectBase* obj, const ObjectBase* versus,
    const Modifier& mod, std::index_sequence<Is...>)
{
    return (calc_mod_input(std::get<Is>(tp), obj, versus, mod, mod.value[Is]) && ...);
}

} // namespace detail

template <typename Callback>
bool Rules::calculate(const ObjectBase* obj, const Modifier& mod, Callback cb,
    const ObjectBase* versus) const
{
    static_assert(detail::function_traits<Callback>::validate_args(), "invalid argument types");

    if (detail::function_traits<Callback>::arity != mod.value.size()) {
        LOG_F(ERROR, "Input/output size mismatch");
        return false;
    }

    if (!meets_requirement(mod.requirement, obj)) {
        return false;
    }

    typename detail::function_traits<Callback>::tuple_type output;
    bool res = detail::calc_mod_inputs(output, obj, versus, mod,
        std::make_integer_sequence<size_t, detail::function_traits<Callback>::arity>{});

    if (!res) {
        LOG_F(ERROR, "Input/output size mismatch");
        return false;
    }

    std::apply(cb, output);
    return true;
}

template <typename Callback>
bool Rules::calculate(const ObjectBase* obj, const ModifierType type, Callback cb,
    const ObjectBase* versus) const
{
    Modifier temp{type, {}, {}, ModifierSource::unknown, Requirement{}, {}, -1};
    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), temp);

    while (it != std::end(entries_) && it->type == type) {
        if (!calculate(obj, *it, cb, versus)) return false;
        ++it;
    }
    return true;
}

template <typename U, typename Callback>
bool Rules::calculate(const ObjectBase* obj, const ModifierType type, U subtype, Callback cb,
    const ObjectBase* versus) const
{
    Modifier temp{type, {}, {}, ModifierSource::unknown, Requirement{}, {}, *subtype};
    auto it = std::lower_bound(std::begin(entries_), std::end(entries_), temp);

    while (it != std::end(entries_) && it->type == type && it->subtype == *subtype) {
        if (!calculate(obj, *it, cb, versus)) return false;
        ++it;
    }
    return true;
}

inline Rules& rules()
{
    auto res = detail::s_services.get_mut<Rules>();
    return res ? *res : *detail::s_services.add<Rules>();
}

} // namespace nw::kernel
