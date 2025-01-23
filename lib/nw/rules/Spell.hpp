#pragma once

#include "../resources/Resource.hpp"
#include "../util/enum_flags.hpp"
#include "feats.hpp"
#include "rule_type.hpp"
#include "system.hpp"

namespace nw {

struct TwoDARowView;

enum struct SpellFlags : uint8_t {
    none = 0x0,
    readied = 0x01,
    spontaneous = 0x02,
    unlimited = 0x04,

    any = 0xFF,
};

DEFINE_ENUM_FLAGS(SpellFlags);

DECLARE_RULE_TYPE(MetaMagic);
DECLARE_RULE_TYPE(MetaMagicFlag);

constexpr MetaMagicFlag metamagic_none = MetaMagicFlag::make(0);
constexpr MetaMagicFlag metamagic_any = MetaMagicFlag::make(0xFF);

struct MetaMagicInfo {
    MetaMagicInfo() = default;
    MetaMagicInfo(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    int level_adjustment = 0;
    Feat feat;
    Requirement requirements;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

using MetaMagicArray = RuleTypeArray<MetaMagic, MetaMagicInfo>;

DECLARE_RULE_TYPE(SpellSchool);

struct SpellSchoolInfo {
    SpellSchoolInfo() = default;
    SpellSchoolInfo(const TwoDARowView& tda);

    String letter;
    uint32_t name = 0xFFFFFFFF;
    SpellSchool opposition = SpellSchool::invalid();
    uint32_t description = 0xFFFFFFFF;
};

/// Spell School singleton component
using SpellSchoolArray = RuleTypeArray<SpellSchool, SpellSchoolInfo>;

DECLARE_RULE_TYPE(Spell);

struct SpellInfo {
    SpellInfo() = default;
    SpellInfo(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    Resref icon;
    SpellSchool school = SpellSchool::invalid();
    // Range
    // VS
    MetaMagicFlag metamagic = metamagic_none;
    // TargetType
    // ImpactScript
    int innate_level = -1; // Class spells and levels are in Class Array
    // ConjTime
    // ConjAnim
    // ConjHeadVisual
    // ConjHandVisual
    // ConjGrndVisual
    // ConjSoundVFX
    // ConjSoundMale
    // ConjSoundFemale
    // CastAnim
    // CastTime
    // CastHeadVisual
    // CastHandVisual
    // CastGrndVisual
    // CastSound
    // Proj
    // ProjModel
    // ProjType
    // ProjSpwnPoint
    // ProjSound
    // ProjOrientation
    // ImmunityType
    // ItemImmunity
    // SubRadSpell1, SubRadSpell2, SubRadSpell3, SubRadSpell4, SubRadSpell5
    // Category
    // Master
    int user_type = 0;
    // SpellDesc
    // UseConcentration
    // SpontaneouslyCast
    // AltMessage
    // HostileSetting
    // FeatID
    // Counter1
    // Counter2
    // HasProjectile

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Spell singleton component
using SpellArray = RuleTypeArray<Spell, SpellInfo>;

} // namespace nw
