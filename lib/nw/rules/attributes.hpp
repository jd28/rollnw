#pragma once

#include "../resources/assets.hpp"
#include "../util/enum_flags.hpp"
#include "rule_type.hpp"

namespace nw {

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

DEFINE_ENUM_FLAGS(AlignmentFlags)

constexpr AlignmentFlags align_lawful_good = AlignmentFlags::lawful | AlignmentFlags::good;
constexpr AlignmentFlags align_neutral_good = AlignmentFlags::neutral | AlignmentFlags::good;
constexpr AlignmentFlags align_chaotic_good = AlignmentFlags::chaotic | AlignmentFlags::good;
constexpr AlignmentFlags align_lawful_neutral = AlignmentFlags::neutral | AlignmentFlags::lawful;
constexpr AlignmentFlags align_true_neutral = AlignmentFlags::neutral;
constexpr AlignmentFlags align_chaotic_neutral = AlignmentFlags::neutral | AlignmentFlags::chaotic;
constexpr AlignmentFlags align_lawful_evil = AlignmentFlags::lawful | AlignmentFlags::evil;
constexpr AlignmentFlags align_neutral_evil = AlignmentFlags::neutral | AlignmentFlags::evil;
constexpr AlignmentFlags align_chaotic_evil = AlignmentFlags::chaotic | AlignmentFlags::evil;

// -- Appearance --------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Appearance);

enum struct AppearanceModelType : int32_t {
    invalid = -1,
    parts = 0,
    simple = 1,
    full = 2,
    limited = 3,
};

enum struct AppearanceModelFlags : uint32_t {
    none = 0,
    wings_allowed = 1 << 0,
    tail_allowed = 1 << 1,
};

DEFINE_ENUM_FLAGS(AppearanceModelFlags)

constexpr auto appearance_model_flags_all = AppearanceModelFlags::wings_allowed
    | AppearanceModelFlags::tail_allowed;

struct AppearanceInfo {
    String label;
    uint32_t string_ref = std::numeric_limits<uint32_t>::max();
    String base_name;
    Resref model;
    AppearanceModelType model_type = AppearanceModelType::invalid;
    AppearanceModelFlags model_flags = AppearanceModelFlags::none;
    float wing_tail_scale = 1.0f;
    float helmet_scale_m = 1.0f;
    float helmet_scale_f = 1.0f;
    float weapon_scale = -1.0f;
    float personal_space = -1.0f;
    bool has_arms = false;

    /// Gets the name to display when using in contexts like a toolset.
    String editor_name() const;

    bool valid() const noexcept { return string_ref != 0xFFFFFFFF || !label.empty(); }
};

using AppearanceArray = RuleTypeArray<Appearance, AppearanceInfo>;

// -- Creature Accessory Models ----------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(WingModel);
DECLARE_RULE_TYPE(TailModel);

struct CreatureAccessoryModelInfo {
    String label;
    Resref model;

    /// Gets the name to display when using in contexts like a toolset.
    String editor_name() const;
    bool valid() const noexcept { return !label.empty() || !model.empty(); }
};

using WingModelArray = RuleTypeArray<WingModel, CreatureAccessoryModelInfo>;
using TailModelArray = RuleTypeArray<TailModel, CreatureAccessoryModelInfo>;

// -- Placeable Appearance ---------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(PlaceableAppearance);

struct PlaceableAppearanceInfo {
    String label;
    uint32_t string_ref = std::numeric_limits<uint32_t>::max();
    Resref model;

    String editor_name() const;
    bool valid() const noexcept { return !model.empty(); }
};

using PlaceableAppearanceArray = RuleTypeArray<PlaceableAppearance, PlaceableAppearanceInfo>;

// -- Door Appearance --------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(DoorType);
DECLARE_RULE_TYPE(GenericDoor);

struct DoorTypeInfo {
    uint32_t string_ref = std::numeric_limits<uint32_t>::max();
    Resref model;

    String editor_name() const;
    bool valid() const noexcept { return !model.empty(); }
};

struct GenericDoorInfo {
    uint32_t string_ref = std::numeric_limits<uint32_t>::max();
    Resref model;

    String editor_name() const;
    bool valid() const noexcept { return !model.empty(); }
};

using DoorTypeArray = RuleTypeArray<DoorType, DoorTypeInfo>;
using GenericDoorArray = RuleTypeArray<GenericDoor, GenericDoorInfo>;

// -- Armor Class -------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(ArmorClass);

// -- Phenotype ---------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Phenotype);

// -- Race --------------------------------------------------------------------
// ----------------------------------------------------------------------------

DECLARE_RULE_TYPE(Race);

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

} // namespace nw
