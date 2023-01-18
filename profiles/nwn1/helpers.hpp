#pragma once

#include <nw/rules/Class.hpp>
#include <nw/rules/Spell.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/items.hpp>
#include <nw/rules/system.hpp>

namespace nwn1 {

namespace mod {

#define DECLARE_MOD(name)                                              \
    nw::Modifier name(nw::ModifierVariant value, std::string_view tag, \
        nw::ModifierSource source = nw::ModifierSource::unknown,       \
        nw::Requirement req = nw::Requirement{}, nw::Versus versus = {})

#define DECLARE_MOD_WITH_SUBTYPE(name, type)                                         \
    nw::Modifier name(type subtype, nw::ModifierVariant value, std::string_view tag, \
        nw::ModifierSource source = nw::ModifierSource::unknown,                     \
        nw::Requirement req = nw::Requirement{}, nw::Versus versus = {})

DECLARE_MOD(ability);
DECLARE_MOD(armor_class);
DECLARE_MOD(attack_bonus);
DECLARE_MOD(concealment);
DECLARE_MOD(damage_bonus);
DECLARE_MOD(damage_immunity);
DECLARE_MOD(damage_reduction);
DECLARE_MOD(damage_resist);
DECLARE_MOD(hitpoints);
DECLARE_MOD_WITH_SUBTYPE(ability, nw::Ability);
DECLARE_MOD_WITH_SUBTYPE(armor_class, nw::ArmorClass);
DECLARE_MOD_WITH_SUBTYPE(attack_bonus, nw::AttackType);
DECLARE_MOD_WITH_SUBTYPE(damage_bonus, nw::AttackType);
DECLARE_MOD_WITH_SUBTYPE(damage_resist, nw::Damage);
DECLARE_MOD_WITH_SUBTYPE(skill, nw::Skill);

#undef DECLARE_MOD
#undef DECLARE_MOD_WITH_SUBTYPE

} // namespace mod

namespace qual {

nw::Qualifier ability(nw::Ability id, int min, int max = 0);
nw::Qualifier alignment(nw::AlignmentAxis axis, nw::AlignmentFlags flags);
nw::Qualifier base_attack_bonus(int min, int max = 0);
nw::Qualifier class_level(nw::Class id, int min, int max = 0);
nw::Qualifier level(int min, int max = 0);
nw::Qualifier feat(nw::Feat id);
nw::Qualifier race(nw::Race id);
nw::Qualifier skill(nw::Skill id, int min, int max = 0);

} // namespace qual

// Convenience functions
namespace sel {

nw::Selector ability(nw::Ability id);
nw::Selector alignment(nw::AlignmentAxis id);
nw::Selector base_attack_bonus();
// nw::Selector armor_class(nw::Index id, bool base = false);
nw::Selector class_level(nw::Class id);
nw::Selector feat(nw::Feat id);
nw::Selector level();
nw::Selector local_var_int(std::string_view var);
nw::Selector local_var_str(std::string_view var);
nw::Selector skill(nw::Skill id);
nw::Selector race();

} // namespace sel

} // namespace nwn1
