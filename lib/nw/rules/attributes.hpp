#pragma once

#include "../resources/Resource.hpp"
#include "../util/enum_flags.hpp"
#include "rule_type.hpp"

namespace nw {

// Forward Decls
struct TwoDARowView;

// -- Ability -----------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Ability);

// Since there is no 2da for abilities, this is a placeholder.

struct AbilityInfo {
    uint32_t name = 0xFFFFFFFF;
    InternedString constant;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Ability singleton component
using AbilityArray = RuleTypeArray<Ability, AbilityInfo>;

// -- Alignment ---------------------------------------------------------------
// ----------------------------------------------------------------------------

enum struct AlignmentAxis {
    neither = 0x0,
    law_chaos = 0x1,
    good_evil = 0x2,
    both = 0x3,
};

DEFINE_ENUM_FLAGS(AlignmentAxis)

enum struct AlignmentType {
    all = 0,
    neutral = 1,
    lawful = 2,
    chaotic = 3,
    good = 4,
    evil = 5,
};

enum struct AlignmentFlags {
    none = 0x0,
    neutral = 0x01,
    lawful = 0x02,
    chaotic = 0x04,
    good = 0x08,
    evil = 0x10,
};

/// Gets alignment axis from alignment flags
AlignmentAxis alignment_axis_from_flags(AlignmentFlags flags);

DEFINE_ENUM_FLAGS(AlignmentFlags)

// -- Armor Class -------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(ArmorClass);

// -- Race --------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Race);

/// Race definition
struct RaceInfo {
    RaceInfo() = default;
    RaceInfo(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    uint32_t name_conversation = 0xFFFFFFFF;
    uint32_t name_conversation_lower = 0xFFFFFFFF;
    uint32_t name_plural = 0xFFFFFFFF;
    uint32_t description = 0xFFFFFFFF;
    Resource icon;
    int appearance = 0;
    std::array<int, 6> ability_modifiers;
    int favored_class = 0;
    Resource feats_table;
    uint32_t biography = 0xFFFFFFFF;
    bool player_race = false;
    InternedString constant;
    int age = 1;
    int toolset_class = 0;
    float cr_modifier = 1.0f;
    int feats_extra_1st_level = 0;
    int skillpoints_extra_per_level = 0;
    int skillpoints_1st_level_multiplier = 0;
    int ability_point_buy_number = 0;
    int feats_normal_level = 0;
    int feats_normal_amount = 0;
    int skillpoints_ability = 0;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Race singleton component
using RaceArray = RuleTypeArray<Race, RaceInfo>;

// Not Implemented Yet
// - NameGenTableA
// - NameGenTableB

// Unimplemented
// - Endurance

// -- Saves -------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Save);
DECLARE_RULE_TYPE(SaveVersus);

// -- Skill -------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Skill);

// Ignored 2da columns: Category, MaxCR

/// Skill definition
struct SkillInfo {
    SkillInfo() = default;
    SkillInfo(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    uint32_t description = 0xFFFFFFFF;
    Resource icon;
    bool untrained = false;
    Ability ability{};
    bool armor_check_penalty = false;
    bool all_can_use = false;
    InternedString constant;
    bool hostile = false;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Singleton Component for Skills
using SkillArray = RuleTypeArray<Skill, SkillInfo>;

} // namespace nw
