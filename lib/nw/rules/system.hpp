#pragma once

#include "../kernel/Memory.hpp"
#include "../kernel/Strings.hpp"
#include "../objects/ObjectBase.hpp"
#include "../util/Variant.hpp"
#include "Versus.hpp"
#include "attributes.hpp"
#include "damage.hpp"
#include "rule_type.hpp"

#include <absl/container/fixed_array.h>
#include <absl/container/inlined_vector.h>

namespace nw {

struct Class;
struct Feat;
struct ObjectBase;

using RuleValue = Variant<int32_t, float, String>;

DECLARE_RULE_TYPE(ReqType);

/// Requirement types
constexpr ReqType req_type_ability = nw::ReqType::make(0);        ///< Subtype: `nw::Ability` constant
constexpr ReqType req_type_ac = nw::ReqType::make(1);             ///< Subtype: ac_* constant
constexpr ReqType req_type_alignment = nw::ReqType::make(2);      ///< Subtype: AlignmentAxis
constexpr ReqType req_type_arcane_level = nw::ReqType::make(3);   ///< Subtype: none
constexpr ReqType req_type_bab = nw::ReqType::make(4);            ///< Subtype: none
constexpr ReqType req_type_caster_level = nw::ReqType::make(5);   ///< Subtype:
constexpr ReqType req_type_class_level = nw::ReqType::make(6);    ///< Subtype: `nw::Class` constant
constexpr ReqType req_type_feat = nw::ReqType::make(7);           ///< Subtype: `nw::Feat` constant
constexpr ReqType req_type_level = nw::ReqType::make(8);          ///< Subtype: none
constexpr ReqType req_type_local_var_int = nw::ReqType::make(9);  ///< Subtype: local var name, eg. "X1_AllowArcher"
constexpr ReqType req_type_local_var_str = nw::ReqType::make(10); ///< Subtype: local var name, eg. "some_var"
constexpr ReqType req_type_race = nw::ReqType::make(11);          ///< Subtype: none
constexpr ReqType req_type_skill = nw::ReqType::make(12);         ///< Subtype: skill_* constant
constexpr ReqType req_type_spell_level = nw::ReqType::make(13);   ///< Subtype:

// == Qualifier ===============================================================
// ============================================================================

struct Qualifier {
    ReqType type;
    RuleValue subtype;
    absl::InlinedVector<RuleValue, 4> params;
};

Qualifier qualifier_ability(Ability id, int min, int max = 0);
Qualifier qualifier_alignment(AlignmentAxis axis, AlignmentFlags flags);
Qualifier qualifier_base_attack_bonus(int min, int max = 0);
Qualifier qualifier_class_level(Class id, int min, int max = 0);
Qualifier qualifier_level(int min, int max = 0);
Qualifier qualifier_feat(Feat id);
Qualifier qualifier_race(Race id);
Qualifier qualifier_skill(Skill id, int min, int max = 0);

// == Requirement =============================================================
// ============================================================================

struct Requirement {
    explicit Requirement(bool conjunction_ = true);
    explicit Requirement(std::initializer_list<Qualifier> quals, bool conjunction_ = true);
    void add(Qualifier qualifier);
    size_t size() const noexcept;

    absl::InlinedVector<Qualifier, 8> qualifiers;
    bool conjunction = true;
};

// == Modifier ================================================================
// ============================================================================

DECLARE_RULE_TYPE(ModifierType);

enum struct ModifierSource {
    unknown,
    ability,
    class_,
    combat_mode,
    environment,
    feat,
    race,
    situation,
    skill,
};

struct ModifierResult {
    ModifierResult() {};
    ModifierResult(int value);
    ModifierResult(float value);
    ModifierResult(DamageRoll value);

    template <typename T>
    bool is() const noexcept;

    template <typename T>
    T as() const noexcept;

private:
    enum struct type {
        none,
        int_,
        float_,
        dmg_roll,
    };

    union {
        int integer;
        float number;
        DamageRoll dmg_roll;
    };
    type type_ = type::none;
};

template <>
inline bool ModifierResult::is<int>() const noexcept { return type_ == type::int_; }

template <>
inline bool ModifierResult::is<float>() const noexcept { return type_ == type::float_; }

template <>
inline bool ModifierResult::is<DamageRoll>() const noexcept { return type_ == type::dmg_roll; }

template <>
inline int ModifierResult::as<int>() const noexcept
{
    return type_ == type::int_ ? integer : 0;
}

template <>
inline float ModifierResult::as<float>() const noexcept
{
    return type_ == type::float_ ? number : 0.0f;
}

template <>
inline DamageRoll ModifierResult::as<DamageRoll>() const noexcept
{
    return type_ == type::dmg_roll ? dmg_roll : DamageRoll{};
}

using ModifierFunction = std::function<ModifierResult(const ObjectBase*, const ObjectBase*, int32_t)>;

using ModifierVariant = Variant<
    int,
    float,
    DamageRoll,
    ModifierFunction
    // Ultimately some script type or chunk here.
    >;

struct Modifier {
    ModifierType type = ModifierType::invalid();
    ModifierVariant input;
    InternedString tagged;
    ModifierSource source = ModifierSource::unknown;
    Requirement requirement = Requirement{};
    int subtype = -1;
};

nw::Modifier make_modifier(nw::ModifierType type, nw::ModifierVariant value,
    StringView tag, nw::ModifierSource source = nw::ModifierSource::unknown,
    nw::Requirement req = nw::Requirement{}, int32_t subtype = -1);

template <typename SubType>
nw::Modifier make_modifier(nw::ModifierType type, SubType subtype, nw::ModifierVariant value,
    StringView tag, nw::ModifierSource source = nw::ModifierSource::unknown,
    nw::Requirement req = nw::Requirement{})
{
    static_assert(nw::is_rule_type<SubType>::value, "Subtype must be a rule type!");
    return make_modifier(type, std::move(value), tag, source, std::move(req), *subtype);
}

inline bool operator<(const Modifier& lhs, const Modifier& rhs)
{
    return std::tie(lhs.type, lhs.subtype, lhs.source) < std::tie(rhs.type, rhs.subtype, rhs.source);
}

struct ModifierRegistry {
    using Storage = Vector<Modifier>;
    using iterator = Storage::iterator;
    using const_iterator = Storage::const_iterator;

    ModifierRegistry(MemoryResource* allocator = kernel::global_allocator());

    /// Adds a modifier to the system
    void add(Modifier mod);

    iterator begin();
    const_iterator begin() const;
    const_iterator cbegin() const;

    /// Clears all modifiers
    void clear();

    iterator end();
    const_iterator end() const;
    const_iterator cend() const;

    /**
     * @brief Removes modifiers by tag
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @return int number of modifiers affected
     */
    int remove(StringView tag);

    /**
     * @brief Replace modifier value
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @param value new value
     * @return int number of modifiers affected
     */
    int replace(StringView tag, ModifierVariant value);

    /**
     * @brief Replace modifier requirement
     *
     * @param tag if ``string_view`` ends with '*' then matches any tag that starts with ``tag``
     * @param req new requirement
     * @return int number of modifiers affected
     */
    int replace(StringView tag, const Requirement& req);

    /// Gets the number of modifiers
    size_t size() const;

private:
    Storage entries_;
    MemoryResource* allocator_ = nullptr;
};

} // namespace nw
