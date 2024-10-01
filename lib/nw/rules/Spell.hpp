#pragma once

#include "../resources/Resource.hpp"
#include "rule_type.hpp"

namespace nw {

struct TwoDARowView;

enum struct SpellFlags : uint8_t {
    none = 0x0,
    readied = 0x01,
    spontaneous = 0x02,
    unlimited = 0x04,
};

enum struct SpellMetaMagic : uint8_t {
    none = 0x00,
    empower = 0x01,
    extend = 0x02,
    maximize = 0x04,
    quicken = 0x08,
    silent = 0x10,
    still = 0x20,
};

DECLARE_RULE_TYPE(SpellSchool);

struct SpellSchoolInfo {
    SpellSchoolInfo() = default;
    SpellSchoolInfo(const TwoDARowView& tda);

    String letter;
    uint32_t name = 0xFFFFFFFF;
    nw::SpellSchool opposition = nw::SpellSchool::invalid();
    uint32_t description = 0xFFFFFFFF;
};

/// Spell School singleton component
using SpellSchoolArray = RuleTypeArray<SpellSchool, SpellSchoolInfo>;

DECLARE_RULE_TYPE(Spell);

struct SpellInfo {
    SpellInfo() = default;
    SpellInfo(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    Resource icon;
    nw::SpellSchool school = nw::SpellSchool::invalid();
    // Range
    // VS
    SpellMetaMagic metamagic = SpellMetaMagic::none;
    // TargetType
    // ImpactScript
    // Bard
    // Cleric
    // Druid
    // Paladin
    // Ranger
    // Wiz_Sorc
    int innate_level = 0;
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
    // UserType
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
