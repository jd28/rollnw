#pragma once

#include "../resources/Resource.hpp"
#include "rule_type.hpp"

namespace nw {

struct TwoDARowView;

DECLARE_RULE_TYPE(Spell);

struct SpellInfo {
    SpellInfo() = default;
    SpellInfo(const TwoDARowView& tda);

    uint32_t name = 0xFFFFFFFF;
    Resource icon;
    // School
    // Range
    // VS
    // MetaMagic
    // TargetType
    // ImpactScript
    // Bard
    // Cleric
    // Druid
    // Paladin
    // Ranger
    // Wiz_Sorc
    // Innate
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
