#pragma once

#include "attributes.hpp"
#include "rule_type.hpp"

#include <absl/container/inlined_vector.h>

#include <cstdint>

namespace nw {

struct Class;
struct Feat;
struct ObjectBase;

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

enum struct QualifierMatch : uint8_t {
    eq,
    ne,
    lt,
    lte,
    gt,
    gte,
    any_bits,
    all_bits,
    no_bits,
};

struct Qualifier {
    ReqType type;
    int32_t subtype = -1;
    QualifierMatch match = QualifierMatch::eq;
    int32_t value = 0;
};

bool match_qualifier_value(int32_t actual, QualifierMatch match, int32_t expected) noexcept;

Qualifier make_qualifier(ReqType type, int32_t subtype, QualifierMatch match, int32_t value);
Qualifier qualifier_ability(Ability id, int value, QualifierMatch match = QualifierMatch::gte);
Qualifier qualifier_ability(Ability id, QualifierMatch match, int value);
Qualifier qualifier_alignment(AlignmentAxis axis, AlignmentFlags flags);
Qualifier qualifier_base_attack_bonus(int value, QualifierMatch match = QualifierMatch::gte);
Qualifier qualifier_base_attack_bonus(QualifierMatch match, int value);
Qualifier qualifier_class_level(Class id, int value, QualifierMatch match = QualifierMatch::gte);
Qualifier qualifier_class_level(Class id, QualifierMatch match, int value);
Qualifier qualifier_level(int value, QualifierMatch match = QualifierMatch::gte);
Qualifier qualifier_level(QualifierMatch match, int value);
Qualifier qualifier_feat(Feat id);
Qualifier qualifier_race(Race id);
Qualifier qualifier_skill(Skill id, int value, QualifierMatch match = QualifierMatch::gte);
Qualifier qualifier_skill(Skill id, QualifierMatch match, int value);

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

} // namespace nw
