#pragma once

#include "../log.hpp"
#include "../objects/Creature.hpp"
#include "../objects/ObjectBase.hpp"
#include "../objects/Placeable.hpp"
#include "../objects/Trap.hpp"
#include "../rules/Class.hpp"
#include "../rules/Spell.hpp"
#include "../rules/attributes.hpp"
#include "../rules/feats.hpp"
#include "../rules/items.hpp"
#include "../rules/system.hpp"
#include "../util/FixedVector.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace nw::kernel {

struct Rules : public Service {
    const static std::type_index type_index;

    Rules(MemoryResource* memory);
    virtual ~Rules() = default;

    /// Initializes rules system
    virtual void initialize(ServiceInitTime time) override;

    /// Match
    bool match(const Qualifier& qual, const ObjectBase* obj) const;

    /// Gets maximum spell levels
    size_t maximum_spell_levels() const noexcept { return maximum_spell_levels_; }

    /// Meets requirements
    bool meets_requirement(const Requirement& req, const ObjectBase* obj) const;

    /// Sets a qualifier for a particular requirement type
    void set_qualifier(ReqType type, bool (*qualifier)(const Qualifier&, const ObjectBase*));

    /// Get service stats
    nlohmann::json stats() const override;

    BaseItemArray baseitems;
    ClassArray classes;
    FeatArray feats;
    MetaMagicArray metamagic;
    RaceArray races;
    SpellArray spells;
    SpellSchoolArray spellschools;
    SkillArray skills;
    PhenotypeArray phenotypes;
    AppearanceArray appearances;
    PlaceableArray placeables;
    TrapArray traps;

private:
    std::array<bool (*)(const Qualifier&, const ObjectBase*), 256> qualifiers_;
    size_t maximum_spell_levels_ = 10;
};

inline Rules& rules()
{
    auto res = services().get_mut<Rules>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load rules service");
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
        auto res = in.as<ModifierFunction>()(obj, versus, subtype);
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
inline FixedVector<Modifier>::const_iterator
find_first_modifier_of(It begin, It end, const ModifierType type, int32_t subtype = -1)
{
    Modifier temp{type, {}, {}, ModifierSource::unknown, Requirement{}, subtype};
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

    if (!obj) { return false; }
    if (!rules().meets_requirement(mod.requirement, obj)) {
        return false;
    }

    typename detail::function_traits<Callback>::tuple_type output;
    bool res = detail::calc_mod_input(std::get<0>(output), obj, versus, mod.input, subtype);

    if (!res) {
        LOG_F(ERROR, "[rules] failed to calculate modifier for '{}'", mod.tagged.view());
        return false;
    }

    cb(std::get<0>(output));
    return true;
}

} // namespace nw::kernel
