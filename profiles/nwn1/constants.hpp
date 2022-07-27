#pragma once

#include "constants/const_baseitem.hpp"
#include "constants/const_disease.hpp"
#include "constants/const_feat.hpp"
#include "constants/const_poison.hpp"
#include "constants/const_spell.hpp"

#include <nw/rules/Ability.hpp>
#include <nw/rules/ArmorClass.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Damage.hpp>
#include <nw/rules/Modifier.hpp>
#include <nw/rules/Race.hpp>
#include <nw/rules/Situation.hpp>
#include <nw/rules/Skill.hpp>

namespace nwn1 {

constexpr nw::Ability ability_strength = nw::make_ability(0);
constexpr nw::Ability ability_dexterity = nw::make_ability(1);
constexpr nw::Ability ability_constitution = nw::make_ability(2);
constexpr nw::Ability ability_intelligence = nw::make_ability(3);
constexpr nw::Ability ability_wisdom = nw::make_ability(4);
constexpr nw::Ability ability_charisma = nw::make_ability(5);

constexpr nw::ArmorClass ac_dodge = nw::make_armor_class(0);
constexpr nw::ArmorClass ac_natural = nw::make_armor_class(1);
constexpr nw::ArmorClass ac_armor = nw::make_armor_class(2);
constexpr nw::ArmorClass ac_shield = nw::make_armor_class(3);
constexpr nw::ArmorClass ac_deflection = nw::make_armor_class(4);

constexpr nw::Class class_type_barbarian = nw::make_class(0);
constexpr nw::Class class_type_bard = nw::make_class(1);
constexpr nw::Class class_type_cleric = nw::make_class(2);
constexpr nw::Class class_type_druid = nw::make_class(3);
constexpr nw::Class class_type_fighter = nw::make_class(4);
constexpr nw::Class class_type_monk = nw::make_class(5);
constexpr nw::Class class_type_paladin = nw::make_class(6);
constexpr nw::Class class_type_ranger = nw::make_class(7);
constexpr nw::Class class_type_rogue = nw::make_class(8);
constexpr nw::Class class_type_sorcerer = nw::make_class(9);
constexpr nw::Class class_type_wizard = nw::make_class(10);
constexpr nw::Class class_type_aberration = nw::make_class(11);
constexpr nw::Class class_type_animal = nw::make_class(12);
constexpr nw::Class class_type_construct = nw::make_class(13);
constexpr nw::Class class_type_humanoid = nw::make_class(14);
constexpr nw::Class class_type_monstrous = nw::make_class(15);
constexpr nw::Class class_type_elemental = nw::make_class(16);
constexpr nw::Class class_type_fey = nw::make_class(17);
constexpr nw::Class class_type_dragon = nw::make_class(18);
constexpr nw::Class class_type_undead = nw::make_class(19);
constexpr nw::Class class_type_commoner = nw::make_class(20);
constexpr nw::Class class_type_beast = nw::make_class(21);
constexpr nw::Class class_type_giant = nw::make_class(22);
constexpr nw::Class class_type_magical_beast = nw::make_class(23);
constexpr nw::Class class_type_outsider = nw::make_class(24);
constexpr nw::Class class_type_shapechanger = nw::make_class(25);
constexpr nw::Class class_type_vermin = nw::make_class(26);
constexpr nw::Class class_type_shadowdancer = nw::make_class(27);
constexpr nw::Class class_type_harper = nw::make_class(28);
constexpr nw::Class class_type_arcane_archer = nw::make_class(29);
constexpr nw::Class class_type_assassin = nw::make_class(30);
constexpr nw::Class class_type_blackguard = nw::make_class(31);
constexpr nw::Class class_type_divinechampion = nw::make_class(32);
constexpr nw::Class class_type_divine_champion = nw::make_class(32);
constexpr nw::Class class_type_weapon_master = nw::make_class(33);
constexpr nw::Class class_type_palemaster = nw::make_class(34);
constexpr nw::Class class_type_pale_master = nw::make_class(34);
constexpr nw::Class class_type_shifter = nw::make_class(35);
constexpr nw::Class class_type_dwarvendefender = nw::make_class(36);
constexpr nw::Class class_type_dwarven_defender = nw::make_class(36);
constexpr nw::Class class_type_dragondisciple = nw::make_class(37);
constexpr nw::Class class_type_dragon_disciple = nw::make_class(37);
constexpr nw::Class class_type_ooze = nw::make_class(38);
constexpr nw::Class class_type_eye_of_gruumsh = nw::make_class(39);
constexpr nw::Class class_type_shou_disciple = nw::make_class(40);
constexpr nw::Class class_type_purple_dragon_knight = nw::make_class(41);

constexpr nw::Damage damage_type_bludgeoning = nw::make_damage(0);
constexpr nw::Damage damage_type_piercing = nw::make_damage(1);
constexpr nw::Damage damage_type_slashing = nw::make_damage(2);
constexpr nw::Damage damage_type_magical = nw::make_damage(3);
constexpr nw::Damage damage_type_acid = nw::make_damage(4);
constexpr nw::Damage damage_type_cold = nw::make_damage(5);
constexpr nw::Damage damage_type_divine = nw::make_damage(6);
constexpr nw::Damage damage_type_electrical = nw::make_damage(7);
constexpr nw::Damage damage_type_fire = nw::make_damage(8);
constexpr nw::Damage damage_type_negative = nw::make_damage(9);
constexpr nw::Damage damage_type_positive = nw::make_damage(10);
constexpr nw::Damage damage_type_sonic = nw::make_damage(11);
constexpr nw::Damage damage_type_base_weapon = nw::make_damage(12);

extern const nw::DamageFlag damage_flag_bludgeoning;
extern const nw::DamageFlag damage_flag_piercing;
extern const nw::DamageFlag damage_flag_slashing;
extern const nw::DamageFlag damage_flag_magical;
extern const nw::DamageFlag damage_flag_acid;
extern const nw::DamageFlag damage_flag_cold;
extern const nw::DamageFlag damage_flag_divine;
extern const nw::DamageFlag damage_flag_electrical;
extern const nw::DamageFlag damage_flag_fire;
extern const nw::DamageFlag damage_flag_negative;
extern const nw::DamageFlag damage_flag_positive;
extern const nw::DamageFlag damage_flag_sonic;
extern const nw::DamageFlag damage_flag_base_weapon;

constexpr nw::Race racial_type_dwarf = nw::make_race(0);
constexpr nw::Race racial_type_elf = nw::make_race(1);
constexpr nw::Race racial_type_gnome = nw::make_race(2);
constexpr nw::Race racial_type_halfling = nw::make_race(3);
constexpr nw::Race racial_type_halfelf = nw::make_race(4);
constexpr nw::Race racial_type_halforc = nw::make_race(5);
constexpr nw::Race racial_type_human = nw::make_race(6);
constexpr nw::Race racial_type_aberration = nw::make_race(7);
constexpr nw::Race racial_type_animal = nw::make_race(8);
constexpr nw::Race racial_type_beast = nw::make_race(9);
constexpr nw::Race racial_type_construct = nw::make_race(10);
constexpr nw::Race racial_type_dragon = nw::make_race(11);
constexpr nw::Race racial_type_humanoid_goblinoid = nw::make_race(12);
constexpr nw::Race racial_type_humanoid_monstrous = nw::make_race(13);
constexpr nw::Race racial_type_humanoid_orc = nw::make_race(14);
constexpr nw::Race racial_type_humanoid_reptilian = nw::make_race(15);
constexpr nw::Race racial_type_elemental = nw::make_race(16);
constexpr nw::Race racial_type_fey = nw::make_race(17);
constexpr nw::Race racial_type_giant = nw::make_race(18);
constexpr nw::Race racial_type_magical_beast = nw::make_race(19);
constexpr nw::Race racial_type_outsider = nw::make_race(20);
constexpr nw::Race racial_type_shapechanger = nw::make_race(23);
constexpr nw::Race racial_type_undead = nw::make_race(24);
constexpr nw::Race racial_type_vermin = nw::make_race(25);
constexpr nw::Race racial_type_all = nw::make_race(28);
constexpr nw::Race racial_type_invalid = nw::make_race(28);
constexpr nw::Race racial_type_ooze = nw::make_race(29);

constexpr nw::Situation situation_type_blind = nw::make_situation(0);
constexpr nw::Situation situation_type_coup_de_grace = nw::make_situation(1);
constexpr nw::Situation situation_type_death_attack = nw::make_situation(2);
constexpr nw::Situation situation_type_flanked = nw::make_situation(3);
constexpr nw::Situation situation_type_flat_footed = nw::make_situation(4);
constexpr nw::Situation situation_type_sneak_attack = nw::make_situation(5);

extern const nw::SituationFlag situation_flag_blind;
extern const nw::SituationFlag situation_flag_coup_de_grace;
extern const nw::SituationFlag situation_flag_death_attack;
extern const nw::SituationFlag situation_flag_flanked;
extern const nw::SituationFlag situation_flag_flat_footed;
extern const nw::SituationFlag situation_flag_sneak_attack;

constexpr nw::Skill skill_animal_empathy = nw::make_skill(0);
constexpr nw::Skill skill_concentration = nw::make_skill(1);
constexpr nw::Skill skill_disable_trap = nw::make_skill(2);
constexpr nw::Skill skill_discipline = nw::make_skill(3);
constexpr nw::Skill skill_heal = nw::make_skill(4);
constexpr nw::Skill skill_hide = nw::make_skill(5);
constexpr nw::Skill skill_listen = nw::make_skill(6);
constexpr nw::Skill skill_lore = nw::make_skill(7);
constexpr nw::Skill skill_move_silently = nw::make_skill(8);
constexpr nw::Skill skill_open_lock = nw::make_skill(9);
constexpr nw::Skill skill_parry = nw::make_skill(10);
constexpr nw::Skill skill_perform = nw::make_skill(11);
constexpr nw::Skill skill_persuade = nw::make_skill(12);
constexpr nw::Skill skill_pick_pocket = nw::make_skill(13);
constexpr nw::Skill skill_search = nw::make_skill(14);
constexpr nw::Skill skill_set_trap = nw::make_skill(15);
constexpr nw::Skill skill_spellcraft = nw::make_skill(16);
constexpr nw::Skill skill_spot = nw::make_skill(17);
constexpr nw::Skill skill_taunt = nw::make_skill(18);
constexpr nw::Skill skill_use_magic_device = nw::make_skill(19);
constexpr nw::Skill skill_appraise = nw::make_skill(20);
constexpr nw::Skill skill_tumble = nw::make_skill(21);
constexpr nw::Skill skill_craft_trap = nw::make_skill(22);
constexpr nw::Skill skill_bluff = nw::make_skill(23);
constexpr nw::Skill skill_intimidate = nw::make_skill(24);
constexpr nw::Skill skill_craft_armor = nw::make_skill(25);
constexpr nw::Skill skill_craft_weapon = nw::make_skill(26);
constexpr nw::Skill skill_ride = nw::make_skill(27);

constexpr nw::ModifierType mod_type_ability = nw::make_modifier_type(0);
constexpr nw::ModifierType mod_type_armor_class = nw::make_modifier_type(1);
constexpr nw::ModifierType mod_type_attack_bonus = nw::make_modifier_type(2);
constexpr nw::ModifierType mod_type_concealment = nw::make_modifier_type(3);
constexpr nw::ModifierType mod_type_crit_multiplier = nw::make_modifier_type(4);
constexpr nw::ModifierType mod_type_crit_threat = nw::make_modifier_type(5);
constexpr nw::ModifierType mod_type_dmg_immunity = nw::make_modifier_type(6);
constexpr nw::ModifierType mod_type_dmg_reduction = nw::make_modifier_type(7);
constexpr nw::ModifierType mod_type_dmg_resistance = nw::make_modifier_type(8);
constexpr nw::ModifierType mod_type_hitpoints = nw::make_modifier_type(9);
constexpr nw::ModifierType mod_type_immunity = nw::make_modifier_type(10);
constexpr nw::ModifierType mod_type_initiative = nw::make_modifier_type(11);
constexpr nw::ModifierType mod_type_movement_speed = nw::make_modifier_type(12);
constexpr nw::ModifierType mod_type_regeneration = nw::make_modifier_type(13);
constexpr nw::ModifierType mod_type_save = nw::make_modifier_type(14);
constexpr nw::ModifierType mod_type_skill = nw::make_modifier_type(15);
constexpr nw::ModifierType mod_type_spell_immunity = nw::make_modifier_type(16);
constexpr nw::ModifierType mod_type_spell_resistance = nw::make_modifier_type(17);

} // namespace nwn1
