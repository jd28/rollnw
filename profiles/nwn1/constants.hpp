#pragma once

#include "constants/const_baseitem.hpp"
#include "constants/const_disease.hpp"
#include "constants/const_feat.hpp"
#include "constants/const_poison.hpp"
#include "constants/const_spell.hpp"

#include <nw/rules/Ability.hpp>
#include <nw/rules/ArmorClass.hpp>
#include <nw/rules/Attack.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Damage.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/Modifier.hpp>
#include <nw/rules/Race.hpp>
#include <nw/rules/Situation.hpp>
#include <nw/rules/Skill.hpp>

namespace nwn1 {

constexpr nw::Ability ability_strength = nw::Ability::make(0);
constexpr nw::Ability ability_dexterity = nw::Ability::make(1);
constexpr nw::Ability ability_constitution = nw::Ability::make(2);
constexpr nw::Ability ability_intelligence = nw::Ability::make(3);
constexpr nw::Ability ability_wisdom = nw::Ability::make(4);
constexpr nw::Ability ability_charisma = nw::Ability::make(5);

constexpr nw::ArmorClass ac_dodge = nw::ArmorClass::make(0);
constexpr nw::ArmorClass ac_natural = nw::ArmorClass::make(1);
constexpr nw::ArmorClass ac_armor = nw::ArmorClass::make(2);
constexpr nw::ArmorClass ac_shield = nw::ArmorClass::make(3);
constexpr nw::ArmorClass ac_deflection = nw::ArmorClass::make(4);

constexpr nw::AttackType attack_type_onhand = nw::AttackType::make(0);
constexpr nw::AttackType attack_type_offhand = nw::AttackType::make(1);
constexpr nw::AttackType attack_type_unarmed = nw::AttackType::make(2);
constexpr nw::AttackType attack_type_cweapon1 = nw::AttackType::make(3);
constexpr nw::AttackType attack_type_cweapon2 = nw::AttackType::make(4);
constexpr nw::AttackType attack_type_cweapon3 = nw::AttackType::make(5);

constexpr nw::Class class_type_barbarian = nw::Class::make(0);
constexpr nw::Class class_type_bard = nw::Class::make(1);
constexpr nw::Class class_type_cleric = nw::Class::make(2);
constexpr nw::Class class_type_druid = nw::Class::make(3);
constexpr nw::Class class_type_fighter = nw::Class::make(4);
constexpr nw::Class class_type_monk = nw::Class::make(5);
constexpr nw::Class class_type_paladin = nw::Class::make(6);
constexpr nw::Class class_type_ranger = nw::Class::make(7);
constexpr nw::Class class_type_rogue = nw::Class::make(8);
constexpr nw::Class class_type_sorcerer = nw::Class::make(9);
constexpr nw::Class class_type_wizard = nw::Class::make(10);
constexpr nw::Class class_type_aberration = nw::Class::make(11);
constexpr nw::Class class_type_animal = nw::Class::make(12);
constexpr nw::Class class_type_construct = nw::Class::make(13);
constexpr nw::Class class_type_humanoid = nw::Class::make(14);
constexpr nw::Class class_type_monstrous = nw::Class::make(15);
constexpr nw::Class class_type_elemental = nw::Class::make(16);
constexpr nw::Class class_type_fey = nw::Class::make(17);
constexpr nw::Class class_type_dragon = nw::Class::make(18);
constexpr nw::Class class_type_undead = nw::Class::make(19);
constexpr nw::Class class_type_commoner = nw::Class::make(20);
constexpr nw::Class class_type_beast = nw::Class::make(21);
constexpr nw::Class class_type_giant = nw::Class::make(22);
constexpr nw::Class class_type_magical_beast = nw::Class::make(23);
constexpr nw::Class class_type_outsider = nw::Class::make(24);
constexpr nw::Class class_type_shapechanger = nw::Class::make(25);
constexpr nw::Class class_type_vermin = nw::Class::make(26);
constexpr nw::Class class_type_shadowdancer = nw::Class::make(27);
constexpr nw::Class class_type_harper = nw::Class::make(28);
constexpr nw::Class class_type_arcane_archer = nw::Class::make(29);
constexpr nw::Class class_type_assassin = nw::Class::make(30);
constexpr nw::Class class_type_blackguard = nw::Class::make(31);
constexpr nw::Class class_type_divinechampion = nw::Class::make(32);
constexpr nw::Class class_type_divine_champion = nw::Class::make(32);
constexpr nw::Class class_type_weapon_master = nw::Class::make(33);
constexpr nw::Class class_type_palemaster = nw::Class::make(34);
constexpr nw::Class class_type_pale_master = nw::Class::make(34);
constexpr nw::Class class_type_shifter = nw::Class::make(35);
constexpr nw::Class class_type_dwarvendefender = nw::Class::make(36);
constexpr nw::Class class_type_dwarven_defender = nw::Class::make(36);
constexpr nw::Class class_type_dragondisciple = nw::Class::make(37);
constexpr nw::Class class_type_dragon_disciple = nw::Class::make(37);
constexpr nw::Class class_type_ooze = nw::Class::make(38);
constexpr nw::Class class_type_eye_of_gruumsh = nw::Class::make(39);
constexpr nw::Class class_type_shou_disciple = nw::Class::make(40);
constexpr nw::Class class_type_purple_dragon_knight = nw::Class::make(41);

constexpr nw::EffectType effect_haste = nw::EffectType::make(1);
constexpr nw::EffectType effect_damage_resistance = nw::EffectType::make(2);
constexpr nw::EffectType effect_slow = nw::EffectType::make(3);
constexpr nw::EffectType effect_resurrection = nw::EffectType::make(4);
constexpr nw::EffectType effect_disease = nw::EffectType::make(5);
constexpr nw::EffectType effect_summon_creature = nw::EffectType::make(6);
constexpr nw::EffectType effect_regenerate = nw::EffectType::make(7);
constexpr nw::EffectType effect_setstate = nw::EffectType::make(8);
constexpr nw::EffectType effect_setstate_internal = nw::EffectType::make(9);
constexpr nw::EffectType effect_attack_increase = nw::EffectType::make(10);
constexpr nw::EffectType effect_attack_decrease = nw::EffectType::make(11);
constexpr nw::EffectType effect_damage_reduction = nw::EffectType::make(12);
constexpr nw::EffectType effect_damage_increase = nw::EffectType::make(13);
constexpr nw::EffectType effect_damage_decrease = nw::EffectType::make(14);
constexpr nw::EffectType effect_temporary_hitpoints = nw::EffectType::make(15);
constexpr nw::EffectType effect_damage_immunity_increase = nw::EffectType::make(16);
constexpr nw::EffectType effect_damage_immunity_decrease = nw::EffectType::make(17);
constexpr nw::EffectType effect_entangle = nw::EffectType::make(18);
constexpr nw::EffectType effect_death = nw::EffectType::make(19);
constexpr nw::EffectType effect_knockdown = nw::EffectType::make(20);
constexpr nw::EffectType effect_deaf = nw::EffectType::make(21);
constexpr nw::EffectType effect_immunity = nw::EffectType::make(22);
constexpr nw::EffectType effect_set_ai_state = nw::EffectType::make(23);
constexpr nw::EffectType effect_enemy_attack_bonus = nw::EffectType::make(24);
constexpr nw::EffectType effect_arcane_spell_failure = nw::EffectType::make(25);
constexpr nw::EffectType effect_saving_throw_increase = nw::EffectType::make(26);
constexpr nw::EffectType effect_saving_throw_decrease = nw::EffectType::make(27);
constexpr nw::EffectType effect_movement_speed_increase = nw::EffectType::make(28);
constexpr nw::EffectType effect_movement_speed_decrease = nw::EffectType::make(29);
constexpr nw::EffectType effect_visualeffect = nw::EffectType::make(30);
constexpr nw::EffectType effect_area_of_effect = nw::EffectType::make(31);
constexpr nw::EffectType effect_beam = nw::EffectType::make(32);
constexpr nw::EffectType effect_spell_resistance_increase = nw::EffectType::make(33);
constexpr nw::EffectType effect_spell_resistance_decrease = nw::EffectType::make(34);
constexpr nw::EffectType effect_poison = nw::EffectType::make(35);
constexpr nw::EffectType effect_ability_increase = nw::EffectType::make(36);
constexpr nw::EffectType effect_ability_decrease = nw::EffectType::make(37);
constexpr nw::EffectType effect_damage = nw::EffectType::make(38);
constexpr nw::EffectType effect_heal = nw::EffectType::make(39);
constexpr nw::EffectType effect_link = nw::EffectType::make(40);
constexpr nw::EffectType effect_haste_internal = nw::EffectType::make(41);
constexpr nw::EffectType effect_slow_internal = nw::EffectType::make(42);
constexpr nw::EffectType effect_modifynumattacks = nw::EffectType::make(44);
constexpr nw::EffectType effect_curse = nw::EffectType::make(45);
constexpr nw::EffectType effect_silence = nw::EffectType::make(46);
constexpr nw::EffectType effect_invisibility = nw::EffectType::make(47);
constexpr nw::EffectType effect_ac_increase = nw::EffectType::make(48);
constexpr nw::EffectType effect_ac_decrease = nw::EffectType::make(49);
constexpr nw::EffectType effect_spell_immunity = nw::EffectType::make(50);
constexpr nw::EffectType effect_dispel_all_magic = nw::EffectType::make(51);
constexpr nw::EffectType effect_dispel_best_magic = nw::EffectType::make(52);
constexpr nw::EffectType effect_taunt = nw::EffectType::make(53);
constexpr nw::EffectType effect_light = nw::EffectType::make(54);
constexpr nw::EffectType effect_skill_increase = nw::EffectType::make(55);
constexpr nw::EffectType effect_skill_decrease = nw::EffectType::make(56);
constexpr nw::EffectType effect_hitpointchangewhendying = nw::EffectType::make(57);
constexpr nw::EffectType effect_setwalkanimation = nw::EffectType::make(58);
constexpr nw::EffectType effect_limit_movement_speed = nw::EffectType::make(59);
constexpr nw::EffectType effect_damage_shield = nw::EffectType::make(61);
constexpr nw::EffectType effect_polymorph = nw::EffectType::make(62);
constexpr nw::EffectType effect_sanctuary = nw::EffectType::make(63);
constexpr nw::EffectType effect_timestop = nw::EffectType::make(64);
constexpr nw::EffectType effect_spell_level_absorption = nw::EffectType::make(65);
constexpr nw::EffectType effect_icon = nw::EffectType::make(67);
constexpr nw::EffectType effect_racial_type = nw::EffectType::make(68);
constexpr nw::EffectType effect_vision = nw::EffectType::make(69);
constexpr nw::EffectType effect_seeinvisible = nw::EffectType::make(70);
constexpr nw::EffectType effect_ultravision = nw::EffectType::make(71);
constexpr nw::EffectType effect_trueseeing = nw::EffectType::make(72);
constexpr nw::EffectType effect_blindness = nw::EffectType::make(73);
constexpr nw::EffectType effect_darkness = nw::EffectType::make(74);
constexpr nw::EffectType effect_miss_chance = nw::EffectType::make(75);
constexpr nw::EffectType effect_concealment = nw::EffectType::make(76);
constexpr nw::EffectType effect_turn_resistance_increase = nw::EffectType::make(77);
constexpr nw::EffectType effect_bonus_spell_of_level = nw::EffectType::make(78);
constexpr nw::EffectType effect_disappearappear = nw::EffectType::make(79);
constexpr nw::EffectType effect_disappear = nw::EffectType::make(80);
constexpr nw::EffectType effect_appear = nw::EffectType::make(81);
constexpr nw::EffectType effect_negative_level = nw::EffectType::make(82);
constexpr nw::EffectType effect_bonus_feat = nw::EffectType::make(83);
constexpr nw::EffectType effect_wounding = nw::EffectType::make(84);
constexpr nw::EffectType effect_swarm = nw::EffectType::make(85);
constexpr nw::EffectType effect_vampiric_regeneration = nw::EffectType::make(86);
constexpr nw::EffectType effect_disarm = nw::EffectType::make(87);
constexpr nw::EffectType effect_turn_resistance_decrease = nw::EffectType::make(88);
constexpr nw::EffectType effect_blindness_inactive = nw::EffectType::make(89);
constexpr nw::EffectType effect_petrify = nw::EffectType::make(90);
constexpr nw::EffectType effect_item_property = nw::EffectType::make(91);
constexpr nw::EffectType effect_spell_failure = nw::EffectType::make(92);
constexpr nw::EffectType effect_cutscene_ghost = nw::EffectType::make(93);
constexpr nw::EffectType effect_cutscene_immobile = nw::EffectType::make(94);
constexpr nw::EffectType effect_defensive_stance = nw::EffectType::make(95);

constexpr nw::Damage damage_type_bludgeoning = nw::Damage::make(0);
constexpr nw::Damage damage_type_piercing = nw::Damage::make(1);
constexpr nw::Damage damage_type_slashing = nw::Damage::make(2);
constexpr nw::Damage damage_type_magical = nw::Damage::make(3);
constexpr nw::Damage damage_type_acid = nw::Damage::make(4);
constexpr nw::Damage damage_type_cold = nw::Damage::make(5);
constexpr nw::Damage damage_type_divine = nw::Damage::make(6);
constexpr nw::Damage damage_type_electrical = nw::Damage::make(7);
constexpr nw::Damage damage_type_fire = nw::Damage::make(8);
constexpr nw::Damage damage_type_negative = nw::Damage::make(9);
constexpr nw::Damage damage_type_positive = nw::Damage::make(10);
constexpr nw::Damage damage_type_sonic = nw::Damage::make(11);
constexpr nw::Damage damage_type_base_weapon = nw::Damage::make(12);

constexpr nw::DamageModType damage_mod_immunity = nw::DamageModType::make(0);
constexpr nw::DamageModType damage_mod_resistance = nw::DamageModType::make(1);
constexpr nw::DamageModType damage_mod_reduction = nw::DamageModType::make(2);

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

constexpr nw::Race racial_type_dwarf = nw::Race::make(0);
constexpr nw::Race racial_type_elf = nw::Race::make(1);
constexpr nw::Race racial_type_gnome = nw::Race::make(2);
constexpr nw::Race racial_type_halfling = nw::Race::make(3);
constexpr nw::Race racial_type_halfelf = nw::Race::make(4);
constexpr nw::Race racial_type_halforc = nw::Race::make(5);
constexpr nw::Race racial_type_human = nw::Race::make(6);
constexpr nw::Race racial_type_aberration = nw::Race::make(7);
constexpr nw::Race racial_type_animal = nw::Race::make(8);
constexpr nw::Race racial_type_beast = nw::Race::make(9);
constexpr nw::Race racial_type_construct = nw::Race::make(10);
constexpr nw::Race racial_type_dragon = nw::Race::make(11);
constexpr nw::Race racial_type_humanoid_goblinoid = nw::Race::make(12);
constexpr nw::Race racial_type_humanoid_monstrous = nw::Race::make(13);
constexpr nw::Race racial_type_humanoid_orc = nw::Race::make(14);
constexpr nw::Race racial_type_humanoid_reptilian = nw::Race::make(15);
constexpr nw::Race racial_type_elemental = nw::Race::make(16);
constexpr nw::Race racial_type_fey = nw::Race::make(17);
constexpr nw::Race racial_type_giant = nw::Race::make(18);
constexpr nw::Race racial_type_magical_beast = nw::Race::make(19);
constexpr nw::Race racial_type_outsider = nw::Race::make(20);
constexpr nw::Race racial_type_shapechanger = nw::Race::make(23);
constexpr nw::Race racial_type_undead = nw::Race::make(24);
constexpr nw::Race racial_type_vermin = nw::Race::make(25);
constexpr nw::Race racial_type_all = nw::Race::make(28);
constexpr nw::Race racial_type_invalid = nw::Race::make(28);
constexpr nw::Race racial_type_ooze = nw::Race::make(29);

constexpr nw::Situation situation_type_blind = nw::Situation::make(0);
constexpr nw::Situation situation_type_coup_de_grace = nw::Situation::make(1);
constexpr nw::Situation situation_type_death_attack = nw::Situation::make(2);
constexpr nw::Situation situation_type_flanked = nw::Situation::make(3);
constexpr nw::Situation situation_type_flat_footed = nw::Situation::make(4);
constexpr nw::Situation situation_type_sneak_attack = nw::Situation::make(5);

extern const nw::SituationFlag situation_flag_blind;
extern const nw::SituationFlag situation_flag_coup_de_grace;
extern const nw::SituationFlag situation_flag_death_attack;
extern const nw::SituationFlag situation_flag_flanked;
extern const nw::SituationFlag situation_flag_flat_footed;
extern const nw::SituationFlag situation_flag_sneak_attack;

constexpr nw::Skill skill_animal_empathy = nw::Skill::make(0);
constexpr nw::Skill skill_concentration = nw::Skill::make(1);
constexpr nw::Skill skill_disable_trap = nw::Skill::make(2);
constexpr nw::Skill skill_discipline = nw::Skill::make(3);
constexpr nw::Skill skill_heal = nw::Skill::make(4);
constexpr nw::Skill skill_hide = nw::Skill::make(5);
constexpr nw::Skill skill_listen = nw::Skill::make(6);
constexpr nw::Skill skill_lore = nw::Skill::make(7);
constexpr nw::Skill skill_move_silently = nw::Skill::make(8);
constexpr nw::Skill skill_open_lock = nw::Skill::make(9);
constexpr nw::Skill skill_parry = nw::Skill::make(10);
constexpr nw::Skill skill_perform = nw::Skill::make(11);
constexpr nw::Skill skill_persuade = nw::Skill::make(12);
constexpr nw::Skill skill_pick_pocket = nw::Skill::make(13);
constexpr nw::Skill skill_search = nw::Skill::make(14);
constexpr nw::Skill skill_set_trap = nw::Skill::make(15);
constexpr nw::Skill skill_spellcraft = nw::Skill::make(16);
constexpr nw::Skill skill_spot = nw::Skill::make(17);
constexpr nw::Skill skill_taunt = nw::Skill::make(18);
constexpr nw::Skill skill_use_magic_device = nw::Skill::make(19);
constexpr nw::Skill skill_appraise = nw::Skill::make(20);
constexpr nw::Skill skill_tumble = nw::Skill::make(21);
constexpr nw::Skill skill_craft_trap = nw::Skill::make(22);
constexpr nw::Skill skill_bluff = nw::Skill::make(23);
constexpr nw::Skill skill_intimidate = nw::Skill::make(24);
constexpr nw::Skill skill_craft_armor = nw::Skill::make(25);
constexpr nw::Skill skill_craft_weapon = nw::Skill::make(26);
constexpr nw::Skill skill_ride = nw::Skill::make(27);

constexpr nw::ModifierType mod_type_ability = nw::ModifierType::make(0);
constexpr nw::ModifierType mod_type_armor_class = nw::ModifierType::make(1);
constexpr nw::ModifierType mod_type_attack_bonus = nw::ModifierType::make(2);
constexpr nw::ModifierType mod_type_concealment = nw::ModifierType::make(3);
constexpr nw::ModifierType mod_type_crit_multiplier = nw::ModifierType::make(4);
constexpr nw::ModifierType mod_type_crit_threat = nw::ModifierType::make(5);
constexpr nw::ModifierType mod_type_dmg_immunity = nw::ModifierType::make(6);
constexpr nw::ModifierType mod_type_dmg_reduction = nw::ModifierType::make(7);
constexpr nw::ModifierType mod_type_dmg_resistance = nw::ModifierType::make(8);
constexpr nw::ModifierType mod_type_hitpoints = nw::ModifierType::make(9);
constexpr nw::ModifierType mod_type_immunity = nw::ModifierType::make(10);
constexpr nw::ModifierType mod_type_initiative = nw::ModifierType::make(11);
constexpr nw::ModifierType mod_type_movement_speed = nw::ModifierType::make(12);
constexpr nw::ModifierType mod_type_regeneration = nw::ModifierType::make(13);
constexpr nw::ModifierType mod_type_save = nw::ModifierType::make(14);
constexpr nw::ModifierType mod_type_skill = nw::ModifierType::make(15);
constexpr nw::ModifierType mod_type_spell_immunity = nw::ModifierType::make(16);
constexpr nw::ModifierType mod_type_spell_resistance = nw::ModifierType::make(17);

} // namespace nwn1
