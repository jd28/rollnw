#pragma once

#include "nw/profiles/nwn1/constants.hpp"

namespace nw {
struct Effect;
struct ItemProperty;
}

namespace nwn1 {

nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier);
nw::Effect* effect_armor_class_modifier(nw::ArmorClass type, int modifier);
nw::Effect* effect_attack_modifier(nw::AttackType attack, int modifier);
nw::Effect* effect_bonus_spell_slot(nw::Class class_, int spell_level);
nw::Effect* effect_damage_immunity(nw::Damage type, int value);
nw::Effect* effect_damage_reduction(int value, int power, int max = 0);
nw::Effect* effect_damage_resistance(nw::Damage type, int value, int max = 0);
nw::Effect* effect_haste();
nw::Effect* effect_hitpoints_temporary(int amount);
nw::Effect* effect_save_modifier(nw::Save save, int modifier, nw::SaveVersus vs = nw::SaveVersus::invalid());
nw::Effect* effect_skill_modifier(nw::Skill skill, int modifier);

nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier);
nw::ItemProperty itemprop_haste();
nw::ItemProperty itemprop_keen();

} // namespace nwn1
