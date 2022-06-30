#pragma once

#include "../resources/Resource.hpp"
#include "type_traits.hpp"

namespace nw {

struct TwoDARowView;

enum struct Spell : int32_t {
    invalid = -1,
};

/// Converts integer into a Spell
constexpr Spell make_spell(int32_t id) { return static_cast<Spell>(id); }

/// Converts integer into a Spell
template <>
struct is_rule_type_base<Spell> : std::true_type {
};

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
struct SpellArray {
    std::vector<SpellInfo> entries;
};

} // namespace nw
