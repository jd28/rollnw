#pragma once

#include "../formats/TwoDA.hpp"
#include "../log.hpp"
#include "../objects/Creature.hpp"
#include "../objects/ObjectBase.hpp"
#include "../rules/Class.hpp"
#include "../rules/Spell.hpp"
#include "../rules/attributes.hpp"
#include "../rules/feats.hpp"
#include "../rules/items.hpp"
#include "../rules/system.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
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

    /// Match
    bool match(const Qualifier& qual, const ObjectBase* obj) const;

    /// Meets requirements
    bool meets_requirement(const Requirement& req, const ObjectBase* obj) const;

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
    ModifierRegistry modifiers;

private:
    qualifier_type qualifier_;
    selector_type selector_;
};

inline Rules& rules()
{
    auto res = services().rules.get();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load rules service");
    }
    return *res;
}

namespace detail {

template <typename T>
constexpr bool is_valid()
{
    return std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_same_v<T, DamageRoll>;
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
    const ModifierVariant& in, int32_t subtype)
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
        auto res = in.as<ModifierSubFunction>()(obj, subtype);
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
        auto res = in.as<ModifierSubVsFunction>()(obj, versus, subtype);
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

template <typename It>
inline std::vector<Modifier>::const_iterator
find_first_modifier_of(It begin, It end, const ModifierType type, int32_t subtype = -1)
{
    Modifier temp{type, {}, {}, ModifierSource::unknown, Requirement{}, {}, subtype};
    auto it = std::lower_bound(begin, end, temp);
    if (it == end) { return end; }
    if (it->type != type) { return end; }
    return it;
}

} // namespace detail

// == Modifiers ===============================================================
// ============================================================================

/**
 * @brief Calculates a modifier
 * @overload resolve_modifier(const ObjectBase* obj, const Modifier& mod, Callback cb, const ObjectBase* versus = nullptr, int32_t subtype = -1)
 * @tparam Callback Modifier callback function
 */
template <typename Callback>
bool resolve_modifier(const ObjectBase* obj, const Modifier& mod, Callback cb,
    const ObjectBase* versus = nullptr, int32_t subtype = -1)
{
    static_assert(detail::function_traits<Callback>::validate_args(), "invalid argument types");
    static_assert(detail::function_traits<Callback>::arity == 1,
        "callbacks can only have one parameter of a modifier result type");

    if (!rules().meets_requirement(mod.requirement, obj)) {
        return false;
    }

    typename detail::function_traits<Callback>::tuple_type output;
    bool res = detail::calc_mod_input(std::get<0>(output), obj, versus, mod.input, subtype);

    if (!res) { return false; }

    cb(std::get<0>(output));
    return true;
}

/**
 * @brief Calculates all modifiers of `type` versus an object
 * @overload resolve_modifier(const ObjectBase* obj, const ModifierType type, const ObjectBase* versus, Callback cb)
 * @tparam Callback Modifier callback function
 */
template <typename Callback>
bool resolve_modifier(const ObjectBase* obj, const ModifierType type,
    const ObjectBase* versus, Callback cb)
{
    auto end = std::cend(rules().modifiers);
    auto it = detail::find_first_modifier_of(std::cbegin(rules().modifiers), end, type, -1);
    while (it != end && it->type == type) {
        if (!resolve_modifier(obj, *it, cb, versus, -1)) return false;
        ++it;
    }
    return true;
}

/**
 * @brief Calculates all modifiers of `type`
 * @overload resolve_modifier(const ObjectBase* obj, const ModifierType type, Callback cb)
 * @tparam Callback Modifier callback function
 */
template <typename Callback>
bool resolve_modifier(const ObjectBase* obj, const ModifierType type, Callback cb)
{
    return resolve_modifier(obj, type, static_cast<const ObjectBase*>(nullptr), cb);
}

/**
 * @brief Calculates all modifiers of a `type` and `subtype` versus another object
 * @overload resolve_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype, const ObjectBase* versus, Callback cb)
 * @tparam U is some rule subtype
 * @tparam Callback Modifier callback function
 */
template <typename SubType, typename Callback>
bool resolve_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype,
    const ObjectBase* versus, Callback cb)
{
    static_assert(is_rule_type<SubType>(), "Subtypes must be rule types");
    std::vector<Modifier>::const_iterator it = std::cbegin(rules().modifiers);
    auto end = std::cend(rules().modifiers);
    if (subtype != SubType::invalid()) {
        it = detail::find_first_modifier_of(it, end, type);
        while (it != end && it->type == type && it->subtype == -1) {
            // Modifier Variant can be applied to any invalid subtype iff it's a function that
            // takes a subtype.
            if (it->input.is<ModifierSubFunction>() || it->input.is<ModifierSubVsFunction>()) {
                if (!resolve_modifier(obj, *it, cb, versus, *subtype)) { return false; }
            }
            ++it;
        }
    }

    it = detail::find_first_modifier_of(it, end, type, *subtype);
    while (it != std::cend(rules().modifiers) && it->type == type && it->subtype == *subtype) {
        if (!resolve_modifier(obj, *it, cb, versus, *subtype)) { return false; }
        ++it;
    }
    return true;
}

/**
 * @brief Calculates all modifiers of a `type` and `subtype`
 * @overload resolve_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype, Callback cb)
 * @tparam U is some rule subtype
 * @tparam Callback Modifier callback function
 */
template <typename SubType, typename Callback>
bool resolve_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype,
    Callback cb)
{
    static_assert(is_rule_type<SubType>(), "Subtypes must be rule types");
    return resolve_modifier(obj, type, subtype, nullptr, cb);
}

/**
 * @brief Maxes all modifiers of `type` versus an object
 * @overload max_modifier(const ObjectBase* obj, const ModifierType type, const ObjectBase* versus)
 * @tparam T
 */
template <typename T>
T max_modifier(const ObjectBase* obj, const ModifierType type,
    const ObjectBase* versus)
{
    T result{};
    auto cb = [&result](const T value) { result = std::max(result, value); };
    resolve_modifier(obj, type, versus, cb);
    return result;
}

/**
 * @brief Maxes all modifiers of `type`
 * @overload max_modifier(const ObjectBase* obj, const ModifierType type)
 * @tparam T
 */
template <typename T>
T max_modifier(const ObjectBase* obj, const ModifierType type)
{
    T result{};
    auto cb = [&result](const T value) { result = std::max(result, value); };
    if (!resolve_modifier(obj, type, static_cast<const ObjectBase*>(nullptr), cb)) { return T{}; }
    return result;
}

/**
 * @brief Maxes all modifiers of a `type` and `subtype` versus another object
 * @overload max_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype, const ObjectBase* versus)
 * @tparam T
 * @tparam U is some rule subtype
 */
template <typename T, typename SubType>
T max_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype,
    const ObjectBase* versus)
{
    T result{};
    auto cb = [&result](const T value) { result = std::max(result, value); };
    if (!resolve_modifier(obj, type, subtype, versus, cb)) { return T{}; }
    return result;
}

/**
 * @brief Maxes all modifiers of a `type` and `subtype`
 * @overload max_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype)
 * @tparam T
 * @tparam U is some rule subtype
 */
template <typename T, typename SubType>
T max_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype)
{
    T result{};
    auto cb = [&result](const T value) { result = std::max(result, value); };
    if (!resolve_modifier(obj, type, subtype, cb)) { return T{}; }
    return result;
}

/**
 * @brief Sums all modifiers of `type` versus an object
 * @overload sum_modifier(const ObjectBase* obj, const ModifierType type, const ObjectBase* versus)
 * @tparam T
 */
template <typename T>
T sum_modifier(const ObjectBase* obj, const ObjectBase* versus, const ModifierType type)
{
    T result{};
    auto cb = [&result](const T value) { result += value; };
    resolve_modifier(obj, type, versus, cb);
    return result;
}

/**
 * @brief Sums all modifiers of `type`
 * @overload sum_modifier(const ObjectBase* obj, const ModifierType type)
 * @tparam T
 */
template <typename T>
T sum_modifier(const ObjectBase* obj, const ModifierType type)
{
    T result{};
    auto cb = [&result](const T value) { result += value; };
    if (!resolve_modifier(obj, type, static_cast<const ObjectBase*>(nullptr), cb)) { return T{}; }
    return result;
}

/**
 * @brief Sums all modifiers of a `type` and `subtype` versus another object
 * @overload sum_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype, const ObjectBase* versus)
 * @tparam T
 * @tparam U is some rule subtype
 */
template <typename T, typename SubType>
T sum_modifier(const ObjectBase* obj, const ObjectBase* versus, const ModifierType type, SubType subtype)
{
    T result{};
    auto cb = [&result](const T value) { result += value; };
    if (!resolve_modifier(obj, type, subtype, versus, cb)) { return T{}; }
    return result;
}

/**
 * @brief Sums all modifiers of a `type` and `subtype`
 * @overload sum_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype)
 * @tparam T
 * @tparam U is some rule subtype
 */
template <typename T, typename SubType>
T sum_modifier(const ObjectBase* obj, const ModifierType type, SubType subtype)
{
    T result{};
    auto cb = [&result](const T value) { result += value; };
    if (!resolve_modifier(obj, type, subtype, cb)) { return T{}; }
    return result;
}

// == Master Feats ============================================================
// ============================================================================

/**
 * @brief Resolves an arbitrary number of master feats
 *
 * @tparam T Return type
 * @tparam U Rule type
 * @tparam Callback Callback type should be ``void(T)``
 * @tparam Args MasterFeat...
 * @param obj Creature object
 * @param type Rule value
 * @param cb This parameter will be called with any valid master feat bonus as a parameter.
 * @param mfeats As many master feats as needed
 */
template <typename T, typename U, typename Callback, typename... Args>
void resolve_master_feats(const Creature* obj, U type, Callback cb, Args... mfeats)
{
    static_assert(is_rule_type<U>::value, "only rule types allowed");
    static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>,
        "result type can only be int or float");

    if (!obj) { return; }

    std::array<T, sizeof...(Args)> result{};
    std::array<MasterFeat, sizeof...(Args)> mfs{mfeats...};
    std::sort(std::begin(mfs), std::end(mfs));

    auto it = std::begin(rules().master_feats.entries());
    auto end = std::end(rules().master_feats.entries());
    size_t i = 0;

    for (auto mf : mfs) {
        MasterFeatEntry mfe{mf, *type, Feat::invalid()};
        const auto& mf_bonus = rules().master_feats.get_bonus(mf);
        if (mf_bonus.empty()) { continue; }

        it = std::lower_bound(it, end, mfe);
        if (it == end) { break; }

        while (it != end && it->type == *type) {
            if (obj->stats.has_feat(it->feat)) {
                if (mf_bonus.template is<T>()) {
                    result[i] = mf_bonus.template as<T>();
                } else if (mf_bonus.template is<ModifierFunction>()) {
                    auto res = mf_bonus.template as<ModifierFunction>()(obj);
                    if (res.template is<T>()) {
                        result[i] = res.template as<T>();
                    }
                }
                break;
            }
            ++it;
        }
        ++i;
    }
    for (const auto& value : result) {
        cb(value);
    }
}

/**
 * @brief Resolves a master feat bonus
 *
 * @tparam T Return type
 * @tparam U Rule type
 * @param obj Creature object
 * @param type Rule value
 * @param mfeat Master feat
 */
template <typename T, typename U>
T resolve_master_feat(const Creature* obj, U type, MasterFeat mfeat)
{
    T result{};
    resolve_master_feats<T>(
        obj, type,
        [&result](T val) { result = val; },
        mfeat);
    return result;
}

/**
 * @brief Sum master feat bonus
 *
 * @tparam T Return type
 * @tparam U Rule type
 * @tparam Args MasterFeat...
 * @param obj Creature object
 * @param type Rule value
 * @param mfeats ``MasterFeat``s
 */
template <typename T, typename U, typename... MasterFeats>
T sum_master_feats(const Creature* obj, U type, MasterFeats... mfeats)
{
    T result{};
    resolve_master_feats<T>(
        obj, type,
        [&result](T val) { result += val; },
        mfeats...);
    return result;
}

} // namespace nw::kernel
