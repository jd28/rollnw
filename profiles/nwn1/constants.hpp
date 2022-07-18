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
#include <nw/rules/Race.hpp>
#include <nw/rules/Situation.hpp>
#include <nw/rules/Skill.hpp>

namespace nwn1 {

extern nw::Ability ability_strength;
extern nw::Ability ability_dexterity;
extern nw::Ability ability_constitution;
extern nw::Ability ability_intelligence;
extern nw::Ability ability_wisdom;
extern nw::Ability ability_charisma;

constexpr nw::ArmorClass ac_dodge = nw::make_armor_class(0);
constexpr nw::ArmorClass ac_natural = nw::make_armor_class(1);
constexpr nw::ArmorClass ac_armor = nw::make_armor_class(2);
constexpr nw::ArmorClass ac_shield = nw::make_armor_class(3);
constexpr nw::ArmorClass ac_deflection = nw::make_armor_class(4);

extern nw::Class class_type_barbarian;
extern nw::Class class_type_bard;
extern nw::Class class_type_cleric;
extern nw::Class class_type_druid;
extern nw::Class class_type_fighter;
extern nw::Class class_type_monk;
extern nw::Class class_type_paladin;
extern nw::Class class_type_ranger;
extern nw::Class class_type_rogue;
extern nw::Class class_type_sorcerer;
extern nw::Class class_type_wizard;
extern nw::Class class_type_aberration;
extern nw::Class class_type_animal;
extern nw::Class class_type_construct;
extern nw::Class class_type_humanoid;
extern nw::Class class_type_monstrous;
extern nw::Class class_type_elemental;
extern nw::Class class_type_fey;
extern nw::Class class_type_dragon;
extern nw::Class class_type_undead;
extern nw::Class class_type_commoner;
extern nw::Class class_type_beast;
extern nw::Class class_type_giant;
extern nw::Class class_type_magical_beast;
extern nw::Class class_type_outsider;
extern nw::Class class_type_shapechanger;
extern nw::Class class_type_vermin;
extern nw::Class class_type_shadowdancer;
extern nw::Class class_type_harper;
extern nw::Class class_type_arcane_archer;
extern nw::Class class_type_assassin;
extern nw::Class class_type_blackguard;
extern nw::Class class_type_divinechampion;
extern nw::Class class_type_divine_champion;
extern nw::Class class_type_weapon_master;
extern nw::Class class_type_palemaster;
extern nw::Class class_type_pale_master;
extern nw::Class class_type_shifter;
extern nw::Class class_type_dwarvendefender;
extern nw::Class class_type_dwarven_defender;
extern nw::Class class_type_dragondisciple;
extern nw::Class class_type_dragon_disciple;
extern nw::Class class_type_ooze;
extern nw::Class class_type_eye_of_gruumsh;
extern nw::Class class_type_shou_disciple;
extern nw::Class class_type_purple_dragon_knight;

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

extern nw::Race racial_type_dwarf;
extern nw::Race racial_type_elf;
extern nw::Race racial_type_gnome;
extern nw::Race racial_type_halfling;
extern nw::Race racial_type_halfelf;
extern nw::Race racial_type_halforc;
extern nw::Race racial_type_human;
extern nw::Race racial_type_aberration;
extern nw::Race racial_type_animal;
extern nw::Race racial_type_beast;
extern nw::Race racial_type_construct;
extern nw::Race racial_type_dragon;
extern nw::Race racial_type_humanoid_goblinoid;
extern nw::Race racial_type_humanoid_monstrous;
extern nw::Race racial_type_humanoid_orc;
extern nw::Race racial_type_humanoid_reptilian;
extern nw::Race racial_type_elemental;
extern nw::Race racial_type_fey;
extern nw::Race racial_type_giant;
extern nw::Race racial_type_magical_beast;
extern nw::Race racial_type_outsider;
extern nw::Race racial_type_shapechanger;
extern nw::Race racial_type_undead;
extern nw::Race racial_type_vermin;
extern nw::Race racial_type_all;
extern nw::Race racial_type_invalid;
extern nw::Race racial_type_ooze;

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

extern nw::Skill skill_animal_empathy;
extern nw::Skill skill_concentration;
extern nw::Skill skill_disable_trap;
extern nw::Skill skill_discipline;
extern nw::Skill skill_heal;
extern nw::Skill skill_hide;
extern nw::Skill skill_listen;
extern nw::Skill skill_lore;
extern nw::Skill skill_move_silently;
extern nw::Skill skill_open_lock;
extern nw::Skill skill_parry;
extern nw::Skill skill_perform;
extern nw::Skill skill_persuade;
extern nw::Skill skill_pick_pocket;
extern nw::Skill skill_search;
extern nw::Skill skill_set_trap;
extern nw::Skill skill_spellcraft;
extern nw::Skill skill_spot;
extern nw::Skill skill_taunt;
extern nw::Skill skill_use_magic_device;
extern nw::Skill skill_appraise;
extern nw::Skill skill_tumble;
extern nw::Skill skill_craft_trap;
extern nw::Skill skill_bluff;
extern nw::Skill skill_intimidate;
extern nw::Skill skill_craft_armor;
extern nw::Skill skill_craft_weapon;
extern nw::Skill skill_ride;

enum MasterFeat {
    mfeat_type_baseitem = 0,
    mfeat_type_skill = 1,
};

enum ModType {
    mod_type_ability = 0,
    mod_type_armor_class = 1,
    mod_type_attack_bonus = 2,
    mod_type_concealment = 3,
    mod_type_crit_multiplier = 4,
    mod_type_crit_threat = 5,
    mod_type_dmg_immunity = 6,
    mod_type_dmg_reduction = 7,
    mod_type_dmg_resistance = 8,
    mod_type_hitpoints = 9,
    mod_type_immunity = 10,
    mod_type_initiative = 11,
    mod_type_movement_speed = 12,
    mod_type_regeneration = 13,
    mod_type_save = 14,
    mod_type_skill = 15,
    mod_type_spell_immunity = 16,
    mod_type_spell_resistance = 17,

};

} // namespace nwn1
