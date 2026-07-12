#pragma once

#include "../resources/assets.hpp"
#include "../util/enum_flags.hpp"
#include "rule_type.hpp"

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
DECLARE_RULE_TYPE(MetaMagicCode);
DECLARE_RULE_TYPE(MetaMagicMask);

constexpr MetaMagicCode metamagic_none = MetaMagicCode::make(0);
constexpr MetaMagicCode metamagic_any = MetaMagicCode::make(0xFF);
constexpr MetaMagicMask metamagic_mask_none = MetaMagicMask::make(0);

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
    MetaMagicMask metamagic_mask = metamagic_mask_none;
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

    /// Gets the name to display when using in contexts like a toolset.
    String editor_name() const;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Spell singleton component
using SpellArray = RuleTypeArray<Spell, SpellInfo>;

} // namespace nw
