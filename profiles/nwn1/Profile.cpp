#include "Profile.hpp"

#include "modifiers.hpp"
#include "rules.hpp"

#include <nw/formats/TwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/Ability.hpp>
#include <nw/rules/BaseItem.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/rules/MasterFeat.hpp>
#include <nw/rules/Race.hpp>
#include <nw/rules/Skill.hpp>
#include <nw/rules/Spell.hpp>
#include <nw/rules/system.hpp>

namespace nwn1 {

bool Profile::load_constants() const
{
    auto* ability_array = nw::kernel::world().get<nw::AbilityArray>();
    if (!ability_array) {
        return false;
    }
    ability_strength = ability_array->from_constant("ABILITY_STRENGTH");
    ability_dexterity = ability_array->from_constant("ABILITY_DEXTERITY");
    ability_constitution = ability_array->from_constant("ABILITY_CONSTITUTION");
    ability_intelligence = ability_array->from_constant("ABILITY_INTELLIGENCE");
    ability_wisdom = ability_array->from_constant("ABILITY_WISDOM");
    ability_charisma = ability_array->from_constant("ABILITY_CHARISMA");

    auto class_array = nw::kernel::world().get<nw::ClassArray>();
    if (!class_array) {
        return false;
    }
    class_type_barbarian = class_array->from_constant("CLASS_TYPE_BARBARIAN");
    class_type_bard = class_array->from_constant("CLASS_TYPE_BARD");
    class_type_cleric = class_array->from_constant("CLASS_TYPE_CLERIC");
    class_type_druid = class_array->from_constant("CLASS_TYPE_DRUID");
    class_type_fighter = class_array->from_constant("CLASS_TYPE_FIGHTER");
    class_type_monk = class_array->from_constant("CLASS_TYPE_MONK");
    class_type_paladin = class_array->from_constant("CLASS_TYPE_PALADIN");
    class_type_ranger = class_array->from_constant("CLASS_TYPE_RANGER");
    class_type_rogue = class_array->from_constant("CLASS_TYPE_ROGUE");
    class_type_sorcerer = class_array->from_constant("CLASS_TYPE_SORCERER");
    class_type_wizard = class_array->from_constant("CLASS_TYPE_WIZARD");
    class_type_aberration = class_array->from_constant("CLASS_TYPE_ABERRATION");
    class_type_animal = class_array->from_constant("CLASS_TYPE_ANIMAL");
    class_type_construct = class_array->from_constant("CLASS_TYPE_CONSTRUCT");
    class_type_humanoid = class_array->from_constant("CLASS_TYPE_HUMANOID");
    class_type_monstrous = class_array->from_constant("CLASS_TYPE_MONSTROUS");
    class_type_elemental = class_array->from_constant("CLASS_TYPE_ELEMENTAL");
    class_type_fey = class_array->from_constant("CLASS_TYPE_FEY");
    class_type_dragon = class_array->from_constant("CLASS_TYPE_DRAGON");
    class_type_undead = class_array->from_constant("CLASS_TYPE_UNDEAD");
    class_type_commoner = class_array->from_constant("CLASS_TYPE_COMMONER");
    class_type_beast = class_array->from_constant("CLASS_TYPE_BEAST");
    class_type_giant = class_array->from_constant("CLASS_TYPE_GIANT");
    class_type_magical_beast = class_array->from_constant("CLASS_TYPE_MAGICAL_BEAST");
    class_type_outsider = class_array->from_constant("CLASS_TYPE_OUTSIDER");
    class_type_shapechanger = class_array->from_constant("CLASS_TYPE_SHAPECHANGER");
    class_type_vermin = class_array->from_constant("CLASS_TYPE_VERMIN");
    class_type_shadowdancer = class_array->from_constant("CLASS_TYPE_SHADOWDANCER");
    class_type_harper = class_array->from_constant("CLASS_TYPE_HARPER");
    class_type_arcane_archer = class_array->from_constant("CLASS_TYPE_ARCANE_ARCHER");
    class_type_assassin = class_array->from_constant("CLASS_TYPE_ASSASSIN");
    class_type_blackguard = class_array->from_constant("CLASS_TYPE_BLACKGUARD");
    class_type_divinechampion = class_array->from_constant("CLASS_TYPE_DIVINECHAMPION");
    class_type_divine_champion = class_array->from_constant("CLASS_TYPE_DIVINE_CHAMPION");
    class_type_weapon_master = class_array->from_constant("CLASS_TYPE_WEAPON_MASTER");
    class_type_palemaster = class_array->from_constant("CLASS_TYPE_PALEMASTER");
    class_type_pale_master = class_array->from_constant("CLASS_TYPE_PALE_MASTER");
    class_type_shifter = class_array->from_constant("CLASS_TYPE_SHIFTER");
    class_type_dwarvendefender = class_array->from_constant("CLASS_TYPE_DWARVENDEFENDER");
    class_type_dwarven_defender = class_array->from_constant("CLASS_TYPE_DWARVEN_DEFENDER");
    class_type_dragondisciple = class_array->from_constant("CLASS_TYPE_DRAGONDISCIPLE");
    class_type_dragon_disciple = class_array->from_constant("CLASS_TYPE_DRAGON_DISCIPLE");
    class_type_ooze = class_array->from_constant("CLASS_TYPE_OOZE");
    class_type_eye_of_gruumsh = class_array->from_constant("CLASS_TYPE_EYE_OF_GRUUMSH");
    class_type_shou_disciple = class_array->from_constant("CLASS_TYPE_SHOU_DISCIPLE");
    class_type_purple_dragon_knight = class_array->from_constant("CLASS_TYPE_PURPLE_DRAGON_KNIGHT");

    auto race_array = nw::kernel::world().get<nw::RaceArray>();
    if (!race_array) {
        return false;
    }
    racial_type_dwarf = race_array->from_constant("RACIAL_TYPE_DWARF");
    racial_type_elf = race_array->from_constant("RACIAL_TYPE_ELF");
    racial_type_gnome = race_array->from_constant("RACIAL_TYPE_GNOME");
    racial_type_halfling = race_array->from_constant("RACIAL_TYPE_HALFLING");
    racial_type_halfelf = race_array->from_constant("RACIAL_TYPE_HALFELF");
    racial_type_halforc = race_array->from_constant("RACIAL_TYPE_HALFORC");
    racial_type_human = race_array->from_constant("RACIAL_TYPE_HUMAN");
    racial_type_aberration = race_array->from_constant("RACIAL_TYPE_ABERRATION");
    racial_type_animal = race_array->from_constant("RACIAL_TYPE_ANIMAL");
    racial_type_beast = race_array->from_constant("RACIAL_TYPE_BEAST");
    racial_type_construct = race_array->from_constant("RACIAL_TYPE_CONSTRUCT");
    racial_type_dragon = race_array->from_constant("RACIAL_TYPE_DRAGON");
    racial_type_humanoid_goblinoid = race_array->from_constant("RACIAL_TYPE_HUMANOID_GOBLINOID");
    racial_type_humanoid_monstrous = race_array->from_constant("RACIAL_TYPE_HUMANOID_MONSTROUS");
    racial_type_humanoid_orc = race_array->from_constant("RACIAL_TYPE_HUMANOID_ORC");
    racial_type_humanoid_reptilian = race_array->from_constant("RACIAL_TYPE_HUMANOID_REPTILIAN");
    racial_type_elemental = race_array->from_constant("RACIAL_TYPE_ELEMENTAL");
    racial_type_fey = race_array->from_constant("RACIAL_TYPE_FEY");
    racial_type_giant = race_array->from_constant("RACIAL_TYPE_GIANT");
    racial_type_magical_beast = race_array->from_constant("RACIAL_TYPE_MAGICAL_BEAST");
    racial_type_outsider = race_array->from_constant("RACIAL_TYPE_OUTSIDER");
    racial_type_shapechanger = race_array->from_constant("RACIAL_TYPE_SHAPECHANGER");
    racial_type_undead = race_array->from_constant("RACIAL_TYPE_UNDEAD");
    racial_type_vermin = race_array->from_constant("RACIAL_TYPE_VERMIN");
    racial_type_all = race_array->from_constant("RACIAL_TYPE_ALL");
    racial_type_invalid = race_array->from_constant("RACIAL_TYPE_INVALID");
    racial_type_ooze = race_array->from_constant("RACIAL_TYPE_OOZE");

    auto skill_array = nw::kernel::world().get<nw::SkillArray>();
    if (!skill_array) {
        return false;
    }
    skill_animal_empathy = skill_array->from_constant("SKILL_ANIMAL_EMPATHY");
    skill_concentration = skill_array->from_constant("SKILL_CONCENTRATION");
    skill_disable_trap = skill_array->from_constant("SKILL_DISABLE_TRAP");
    skill_discipline = skill_array->from_constant("SKILL_DISCIPLINE");
    skill_heal = skill_array->from_constant("SKILL_HEAL");
    skill_hide = skill_array->from_constant("SKILL_HIDE");
    skill_listen = skill_array->from_constant("SKILL_LISTEN");
    skill_lore = skill_array->from_constant("SKILL_LORE");
    skill_move_silently = skill_array->from_constant("SKILL_MOVE_SILENTLY");
    skill_open_lock = skill_array->from_constant("SKILL_OPEN_LOCK");
    skill_parry = skill_array->from_constant("SKILL_PARRY");
    skill_perform = skill_array->from_constant("SKILL_PERFORM");
    skill_persuade = skill_array->from_constant("SKILL_PERSUADE");
    skill_pick_pocket = skill_array->from_constant("SKILL_PICK_POCKET");
    skill_search = skill_array->from_constant("SKILL_SEARCH");
    skill_set_trap = skill_array->from_constant("SKILL_SET_TRAP");
    skill_spellcraft = skill_array->from_constant("SKILL_SPELLCRAFT");
    skill_spot = skill_array->from_constant("SKILL_SPOT");
    skill_taunt = skill_array->from_constant("SKILL_TAUNT");
    skill_use_magic_device = skill_array->from_constant("SKILL_USE_MAGIC_DEVICE");
    skill_appraise = skill_array->from_constant("SKILL_APPRAISE");
    skill_tumble = skill_array->from_constant("SKILL_TUMBLE");
    skill_craft_trap = skill_array->from_constant("SKILL_CRAFT_TRAP");
    skill_bluff = skill_array->from_constant("SKILL_BLUFF");
    skill_intimidate = skill_array->from_constant("SKILL_INTIMIDATE");
    skill_craft_armor = skill_array->from_constant("SKILL_CRAFT_ARMOR");
    skill_craft_weapon = skill_array->from_constant("SKILL_CRAFT_WEAPON");
    skill_ride = skill_array->from_constant("SKILL_RIDE");

    auto feat_array = nw::kernel::world().get<nw::FeatArray>();
    if (!feat_array) {
        return false;
    }
    feat_alertness = feat_array->from_constant("FEAT_ALERTNESS");
    feat_ambidexterity = feat_array->from_constant("FEAT_AMBIDEXTERITY");
    feat_armor_proficiency_heavy = feat_array->from_constant("FEAT_ARMOR_PROFICIENCY_HEAVY");
    feat_armor_proficiency_light = feat_array->from_constant("FEAT_ARMOR_PROFICIENCY_LIGHT");
    feat_armor_proficiency_medium = feat_array->from_constant("FEAT_ARMOR_PROFICIENCY_MEDIUM");
    feat_called_shot = feat_array->from_constant("FEAT_CALLED_SHOT");
    feat_cleave = feat_array->from_constant("FEAT_CLEAVE");
    feat_combat_casting = feat_array->from_constant("FEAT_COMBAT_CASTING");
    feat_deflect_arrows = feat_array->from_constant("FEAT_DEFLECT_ARROWS");
    feat_disarm = feat_array->from_constant("FEAT_DISARM");
    feat_dodge = feat_array->from_constant("FEAT_DODGE");
    feat_empower_spell = feat_array->from_constant("FEAT_EMPOWER_SPELL");
    feat_extend_spell = feat_array->from_constant("FEAT_EXTEND_SPELL");
    feat_extra_turning = feat_array->from_constant("FEAT_EXTRA_TURNING");
    feat_great_fortitude = feat_array->from_constant("FEAT_GREAT_FORTITUDE");
    feat_improved_critical_club = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_CLUB");
    feat_improved_disarm = feat_array->from_constant("FEAT_IMPROVED_DISARM");
    feat_improved_knockdown = feat_array->from_constant("FEAT_IMPROVED_KNOCKDOWN");
    feat_improved_parry = feat_array->from_constant("FEAT_IMPROVED_PARRY");
    feat_improved_power_attack = feat_array->from_constant("FEAT_IMPROVED_POWER_ATTACK");
    feat_improved_two_weapon_fighting = feat_array->from_constant("FEAT_IMPROVED_TWO_WEAPON_FIGHTING");
    feat_improved_unarmed_strike = feat_array->from_constant("FEAT_IMPROVED_UNARMED_STRIKE");
    feat_iron_will = feat_array->from_constant("FEAT_IRON_WILL");
    feat_knockdown = feat_array->from_constant("FEAT_KNOCKDOWN");
    feat_lightning_reflexes = feat_array->from_constant("FEAT_LIGHTNING_REFLEXES");
    feat_maximize_spell = feat_array->from_constant("FEAT_MAXIMIZE_SPELL");
    feat_mobility = feat_array->from_constant("FEAT_MOBILITY");
    feat_point_blank_shot = feat_array->from_constant("FEAT_POINT_BLANK_SHOT");
    feat_power_attack = feat_array->from_constant("FEAT_POWER_ATTACK");
    feat_quicken_spell = feat_array->from_constant("FEAT_QUICKEN_SPELL");
    feat_rapid_shot = feat_array->from_constant("FEAT_RAPID_SHOT");
    feat_sap = feat_array->from_constant("FEAT_SAP");
    feat_shield_proficiency = feat_array->from_constant("FEAT_SHIELD_PROFICIENCY");
    feat_silence_spell = feat_array->from_constant("FEAT_SILENCE_SPELL");
    feat_skill_focus_animal_empathy = feat_array->from_constant("FEAT_SKILL_FOCUS_ANIMAL_EMPATHY");
    feat_spell_focus_abjuration = feat_array->from_constant("FEAT_SPELL_FOCUS_ABJURATION");
    feat_spell_penetration = feat_array->from_constant("FEAT_SPELL_PENETRATION");
    feat_still_spell = feat_array->from_constant("FEAT_STILL_SPELL");
    feat_stunning_fist = feat_array->from_constant("FEAT_STUNNING_FIST");
    feat_toughness = feat_array->from_constant("FEAT_TOUGHNESS");
    feat_two_weapon_fighting = feat_array->from_constant("FEAT_TWO_WEAPON_FIGHTING");
    feat_weapon_finesse = feat_array->from_constant("FEAT_WEAPON_FINESSE");
    feat_weapon_focus_club = feat_array->from_constant("FEAT_WEAPON_FOCUS_CLUB");
    feat_weapon_proficiency_exotic = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_EXOTIC");
    feat_weapon_proficiency_martial = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_MARTIAL");
    feat_weapon_proficiency_simple = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_SIMPLE");
    feat_weapon_specialization_club = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_CLUB");
    feat_weapon_proficiency_druid = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_DRUID");
    feat_weapon_proficiency_monk = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_MONK");
    feat_weapon_proficiency_rogue = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_ROGUE");
    feat_weapon_proficiency_wizard = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_WIZARD");
    feat_improved_critical_dagger = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_DAGGER");
    feat_improved_critical_dart = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_DART");
    feat_improved_critical_heavy_crossbow = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_HEAVY_CROSSBOW");
    feat_improved_critical_light_crossbow = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_LIGHT_CROSSBOW");
    feat_improved_critical_light_mace = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_LIGHT_MACE");
    feat_improved_critical_morning_star = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_MORNING_STAR");
    feat_improved_critical_staff = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_STAFF");
    feat_improved_critical_spear = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SPEAR");
    feat_improved_critical_sickle = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SICKLE");
    feat_improved_critical_sling = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SLING");
    feat_improved_critical_unarmed_strike = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_UNARMED_STRIKE");
    feat_improved_critical_longbow = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_LONGBOW");
    feat_improved_critical_shortbow = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SHORTBOW");
    feat_improved_critical_short_sword = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SHORT_SWORD");
    feat_improved_critical_rapier = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_RAPIER");
    feat_improved_critical_scimitar = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SCIMITAR");
    feat_improved_critical_long_sword = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_LONG_SWORD");
    feat_improved_critical_great_sword = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_GREAT_SWORD");
    feat_improved_critical_hand_axe = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_HAND_AXE");
    feat_improved_critical_throwing_axe = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_THROWING_AXE");
    feat_improved_critical_battle_axe = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_BATTLE_AXE");
    feat_improved_critical_great_axe = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_GREAT_AXE");
    feat_improved_critical_halberd = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_HALBERD");
    feat_improved_critical_light_hammer = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_LIGHT_HAMMER");
    feat_improved_critical_light_flail = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_LIGHT_FLAIL");
    feat_improved_critical_war_hammer = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_WAR_HAMMER");
    feat_improved_critical_heavy_flail = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_HEAVY_FLAIL");
    feat_improved_critical_kama = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_KAMA");
    feat_improved_critical_kukri = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_KUKRI");
    // feat_improved_critical_nunchaku = cr->add( "FEAT_IMPROVED_CRITICAL_NUNCHAKU" , 81);
    feat_improved_critical_shuriken = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SHURIKEN");
    feat_improved_critical_scythe = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_SCYTHE");
    feat_improved_critical_katana = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_KATANA");
    feat_improved_critical_bastard_sword = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_BASTARD_SWORD");
    feat_improved_critical_dire_mace = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_DIRE_MACE");
    feat_improved_critical_double_axe = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_DOUBLE_AXE");
    feat_improved_critical_two_bladed_sword = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_TWO_BLADED_SWORD");
    feat_weapon_focus_dagger = feat_array->from_constant("FEAT_WEAPON_FOCUS_DAGGER");
    feat_weapon_focus_dart = feat_array->from_constant("FEAT_WEAPON_FOCUS_DART");
    feat_weapon_focus_heavy_crossbow = feat_array->from_constant("FEAT_WEAPON_FOCUS_HEAVY_CROSSBOW");
    feat_weapon_focus_light_crossbow = feat_array->from_constant("FEAT_WEAPON_FOCUS_LIGHT_CROSSBOW");
    feat_weapon_focus_light_mace = feat_array->from_constant("FEAT_WEAPON_FOCUS_LIGHT_MACE");
    feat_weapon_focus_morning_star = feat_array->from_constant("FEAT_WEAPON_FOCUS_MORNING_STAR");
    feat_weapon_focus_staff = feat_array->from_constant("FEAT_WEAPON_FOCUS_STAFF");
    feat_weapon_focus_spear = feat_array->from_constant("FEAT_WEAPON_FOCUS_SPEAR");
    feat_weapon_focus_sickle = feat_array->from_constant("FEAT_WEAPON_FOCUS_SICKLE");
    feat_weapon_focus_sling = feat_array->from_constant("FEAT_WEAPON_FOCUS_SLING");
    feat_weapon_focus_unarmed_strike = feat_array->from_constant("FEAT_WEAPON_FOCUS_UNARMED_STRIKE");
    feat_weapon_focus_longbow = feat_array->from_constant("FEAT_WEAPON_FOCUS_LONGBOW");
    feat_weapon_focus_shortbow = feat_array->from_constant("FEAT_WEAPON_FOCUS_SHORTBOW");
    feat_weapon_focus_short_sword = feat_array->from_constant("FEAT_WEAPON_FOCUS_SHORT_SWORD");
    feat_weapon_focus_rapier = feat_array->from_constant("FEAT_WEAPON_FOCUS_RAPIER");
    feat_weapon_focus_scimitar = feat_array->from_constant("FEAT_WEAPON_FOCUS_SCIMITAR");
    feat_weapon_focus_long_sword = feat_array->from_constant("FEAT_WEAPON_FOCUS_LONG_SWORD");
    feat_weapon_focus_great_sword = feat_array->from_constant("FEAT_WEAPON_FOCUS_GREAT_SWORD");
    feat_weapon_focus_hand_axe = feat_array->from_constant("FEAT_WEAPON_FOCUS_HAND_AXE");
    feat_weapon_focus_throwing_axe = feat_array->from_constant("FEAT_WEAPON_FOCUS_THROWING_AXE");
    feat_weapon_focus_battle_axe = feat_array->from_constant("FEAT_WEAPON_FOCUS_BATTLE_AXE");
    feat_weapon_focus_great_axe = feat_array->from_constant("FEAT_WEAPON_FOCUS_GREAT_AXE");
    feat_weapon_focus_halberd = feat_array->from_constant("FEAT_WEAPON_FOCUS_HALBERD");
    feat_weapon_focus_light_hammer = feat_array->from_constant("FEAT_WEAPON_FOCUS_LIGHT_HAMMER");
    feat_weapon_focus_light_flail = feat_array->from_constant("FEAT_WEAPON_FOCUS_LIGHT_FLAIL");
    feat_weapon_focus_war_hammer = feat_array->from_constant("FEAT_WEAPON_FOCUS_WAR_HAMMER");
    feat_weapon_focus_heavy_flail = feat_array->from_constant("FEAT_WEAPON_FOCUS_HEAVY_FLAIL");
    feat_weapon_focus_kama = feat_array->from_constant("FEAT_WEAPON_FOCUS_KAMA");
    feat_weapon_focus_kukri = feat_array->from_constant("FEAT_WEAPON_FOCUS_KUKRI");
    // feat_weapon_focus_nunchaku = cr->add( "FEAT_WEAPON_FOCUS_NUNCHAKU" , 119);
    feat_weapon_focus_shuriken = feat_array->from_constant("FEAT_WEAPON_FOCUS_SHURIKEN");
    feat_weapon_focus_scythe = feat_array->from_constant("FEAT_WEAPON_FOCUS_SCYTHE");
    feat_weapon_focus_katana = feat_array->from_constant("FEAT_WEAPON_FOCUS_KATANA");
    feat_weapon_focus_bastard_sword = feat_array->from_constant("FEAT_WEAPON_FOCUS_BASTARD_SWORD");
    feat_weapon_focus_dire_mace = feat_array->from_constant("FEAT_WEAPON_FOCUS_DIRE_MACE");
    feat_weapon_focus_double_axe = feat_array->from_constant("FEAT_WEAPON_FOCUS_DOUBLE_AXE");
    feat_weapon_focus_two_bladed_sword = feat_array->from_constant("FEAT_WEAPON_FOCUS_TWO_BLADED_SWORD");
    feat_weapon_specialization_dagger = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_DAGGER");
    feat_weapon_specialization_dart = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_DART");
    feat_weapon_specialization_heavy_crossbow = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_HEAVY_CROSSBOW");
    feat_weapon_specialization_light_crossbow = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_LIGHT_CROSSBOW");
    feat_weapon_specialization_light_mace = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_LIGHT_MACE");
    feat_weapon_specialization_morning_star = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_MORNING_STAR");
    feat_weapon_specialization_staff = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_STAFF");
    feat_weapon_specialization_spear = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SPEAR");
    feat_weapon_specialization_sickle = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SICKLE");
    feat_weapon_specialization_sling = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SLING");
    feat_weapon_specialization_unarmed_strike = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_UNARMED_STRIKE");
    feat_weapon_specialization_longbow = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_LONGBOW");
    feat_weapon_specialization_shortbow = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SHORTBOW");
    feat_weapon_specialization_short_sword = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SHORT_SWORD");
    feat_weapon_specialization_rapier = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_RAPIER");
    feat_weapon_specialization_scimitar = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SCIMITAR");
    feat_weapon_specialization_long_sword = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_LONG_SWORD");
    feat_weapon_specialization_great_sword = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_GREAT_SWORD");
    feat_weapon_specialization_hand_axe = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_HAND_AXE");
    feat_weapon_specialization_throwing_axe = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_THROWING_AXE");
    feat_weapon_specialization_battle_axe = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_BATTLE_AXE");
    feat_weapon_specialization_great_axe = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_GREAT_AXE");
    feat_weapon_specialization_halberd = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_HALBERD");
    feat_weapon_specialization_light_hammer = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_LIGHT_HAMMER");
    feat_weapon_specialization_light_flail = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_LIGHT_FLAIL");
    feat_weapon_specialization_war_hammer = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_WAR_HAMMER");
    feat_weapon_specialization_heavy_flail = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_HEAVY_FLAIL");
    feat_weapon_specialization_kama = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_KAMA");
    feat_weapon_specialization_kukri = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_KUKRI");
    // feat_weapon_specialization_nunchaku = cr->add( "FEAT_WEAPON_SPECIALIZATION_NUNCHAKU" , 157);
    feat_weapon_specialization_shuriken = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SHURIKEN");
    feat_weapon_specialization_scythe = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_SCYTHE");
    feat_weapon_specialization_katana = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_KATANA");
    feat_weapon_specialization_bastard_sword = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_BASTARD_SWORD");
    feat_weapon_specialization_dire_mace = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_DIRE_MACE");
    feat_weapon_specialization_double_axe = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_DOUBLE_AXE");
    feat_weapon_specialization_two_bladed_sword = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_TWO_BLADED_SWORD");
    feat_spell_focus_conjuration = feat_array->from_constant("FEAT_SPELL_FOCUS_CONJURATION");
    feat_spell_focus_divination = feat_array->from_constant("FEAT_SPELL_FOCUS_DIVINATION");
    feat_spell_focus_enchantment = feat_array->from_constant("FEAT_SPELL_FOCUS_ENCHANTMENT");
    feat_spell_focus_evocation = feat_array->from_constant("FEAT_SPELL_FOCUS_EVOCATION");
    feat_spell_focus_illusion = feat_array->from_constant("FEAT_SPELL_FOCUS_ILLUSION");
    feat_spell_focus_necromancy = feat_array->from_constant("FEAT_SPELL_FOCUS_NECROMANCY");
    feat_spell_focus_transmutation = feat_array->from_constant("FEAT_SPELL_FOCUS_TRANSMUTATION");
    feat_skill_focus_concentration = feat_array->from_constant("FEAT_SKILL_FOCUS_CONCENTRATION");
    feat_skill_focus_disable_trap = feat_array->from_constant("FEAT_SKILL_FOCUS_DISABLE_TRAP");
    feat_skill_focus_discipline = feat_array->from_constant("FEAT_SKILL_FOCUS_DISCIPLINE");
    feat_skill_focus_heal = feat_array->from_constant("FEAT_SKILL_FOCUS_HEAL");
    feat_skill_focus_hide = feat_array->from_constant("FEAT_SKILL_FOCUS_HIDE");
    feat_skill_focus_listen = feat_array->from_constant("FEAT_SKILL_FOCUS_LISTEN");
    feat_skill_focus_lore = feat_array->from_constant("FEAT_SKILL_FOCUS_LORE");
    feat_skill_focus_move_silently = feat_array->from_constant("FEAT_SKILL_FOCUS_MOVE_SILENTLY");
    feat_skill_focus_open_lock = feat_array->from_constant("FEAT_SKILL_FOCUS_OPEN_LOCK");
    feat_skill_focus_parry = feat_array->from_constant("FEAT_SKILL_FOCUS_PARRY");
    feat_skill_focus_perform = feat_array->from_constant("FEAT_SKILL_FOCUS_PERFORM");
    feat_skill_focus_persuade = feat_array->from_constant("FEAT_SKILL_FOCUS_PERSUADE");
    feat_skill_focus_pick_pocket = feat_array->from_constant("FEAT_SKILL_FOCUS_PICK_POCKET");
    feat_skill_focus_search = feat_array->from_constant("FEAT_SKILL_FOCUS_SEARCH");
    feat_skill_focus_set_trap = feat_array->from_constant("FEAT_SKILL_FOCUS_SET_TRAP");
    feat_skill_focus_spellcraft = feat_array->from_constant("FEAT_SKILL_FOCUS_SPELLCRAFT");
    feat_skill_focus_spot = feat_array->from_constant("FEAT_SKILL_FOCUS_SPOT");
    feat_skill_focus_taunt = feat_array->from_constant("FEAT_SKILL_FOCUS_TAUNT");
    feat_skill_focus_use_magic_device = feat_array->from_constant("FEAT_SKILL_FOCUS_USE_MAGIC_DEVICE");
    feat_barbarian_endurance = feat_array->from_constant("FEAT_BARBARIAN_ENDURANCE");
    feat_uncanny_dodge_1 = feat_array->from_constant("FEAT_UNCANNY_DODGE_1");
    feat_damage_reduction = feat_array->from_constant("FEAT_DAMAGE_REDUCTION");
    feat_bardic_knowledge = feat_array->from_constant("FEAT_BARDIC_KNOWLEDGE");
    feat_nature_sense = feat_array->from_constant("FEAT_NATURE_SENSE");
    feat_animal_companion = feat_array->from_constant("FEAT_ANIMAL_COMPANION");
    feat_woodland_stride = feat_array->from_constant("FEAT_WOODLAND_STRIDE");
    feat_trackless_step = feat_array->from_constant("FEAT_TRACKLESS_STEP");
    feat_resist_natures_lure = feat_array->from_constant("FEAT_RESIST_NATURES_LURE");
    feat_venom_immunity = feat_array->from_constant("FEAT_VENOM_IMMUNITY");
    feat_flurry_of_blows = feat_array->from_constant("FEAT_FLURRY_OF_BLOWS");
    feat_evasion = feat_array->from_constant("FEAT_EVASION");
    feat_monk_endurance = feat_array->from_constant("FEAT_MONK_ENDURANCE");
    feat_still_mind = feat_array->from_constant("FEAT_STILL_MIND");
    feat_purity_of_body = feat_array->from_constant("FEAT_PURITY_OF_BODY");
    feat_wholeness_of_body = feat_array->from_constant("FEAT_WHOLENESS_OF_BODY");
    feat_improved_evasion = feat_array->from_constant("FEAT_IMPROVED_EVASION");
    feat_ki_strike = feat_array->from_constant("FEAT_KI_STRIKE");
    feat_diamond_body = feat_array->from_constant("FEAT_DIAMOND_BODY");
    feat_diamond_soul = feat_array->from_constant("FEAT_DIAMOND_SOUL");
    feat_perfect_self = feat_array->from_constant("FEAT_PERFECT_SELF");
    feat_divine_grace = feat_array->from_constant("FEAT_DIVINE_GRACE");
    feat_divine_health = feat_array->from_constant("FEAT_DIVINE_HEALTH");
    feat_sneak_attack = feat_array->from_constant("FEAT_SNEAK_ATTACK");
    feat_crippling_strike = feat_array->from_constant("FEAT_CRIPPLING_STRIKE");
    feat_defensive_roll = feat_array->from_constant("FEAT_DEFENSIVE_ROLL");
    feat_opportunist = feat_array->from_constant("FEAT_OPPORTUNIST");
    feat_skill_mastery = feat_array->from_constant("FEAT_SKILL_MASTERY");
    feat_uncanny_reflex = feat_array->from_constant("FEAT_UNCANNY_REFLEX");
    feat_stonecunning = feat_array->from_constant("FEAT_STONECUNNING");
    feat_darkvision = feat_array->from_constant("FEAT_DARKVISION");
    feat_hardiness_versus_poisons = feat_array->from_constant("FEAT_HARDINESS_VERSUS_POISONS");
    feat_hardiness_versus_spells = feat_array->from_constant("FEAT_HARDINESS_VERSUS_SPELLS");
    feat_battle_training_versus_orcs = feat_array->from_constant("FEAT_BATTLE_TRAINING_VERSUS_ORCS");
    feat_battle_training_versus_goblins = feat_array->from_constant("FEAT_BATTLE_TRAINING_VERSUS_GOBLINS");
    feat_battle_training_versus_giants = feat_array->from_constant("FEAT_BATTLE_TRAINING_VERSUS_GIANTS");
    feat_skill_affinity_lore = feat_array->from_constant("FEAT_SKILL_AFFINITY_LORE");
    feat_immunity_to_sleep = feat_array->from_constant("FEAT_IMMUNITY_TO_SLEEP");
    feat_hardiness_versus_enchantments = feat_array->from_constant("FEAT_HARDINESS_VERSUS_ENCHANTMENTS");
    feat_skill_affinity_listen = feat_array->from_constant("FEAT_SKILL_AFFINITY_LISTEN");
    feat_skill_affinity_search = feat_array->from_constant("FEAT_SKILL_AFFINITY_SEARCH");
    feat_skill_affinity_spot = feat_array->from_constant("FEAT_SKILL_AFFINITY_SPOT");
    feat_keen_sense = feat_array->from_constant("FEAT_KEEN_SENSE");
    feat_hardiness_versus_illusions = feat_array->from_constant("FEAT_HARDINESS_VERSUS_ILLUSIONS");
    feat_battle_training_versus_reptilians = feat_array->from_constant("FEAT_BATTLE_TRAINING_VERSUS_REPTILIANS");
    feat_skill_affinity_concentration = feat_array->from_constant("FEAT_SKILL_AFFINITY_CONCENTRATION");
    feat_partial_skill_affinity_listen = feat_array->from_constant("FEAT_PARTIAL_SKILL_AFFINITY_LISTEN");
    feat_partial_skill_affinity_search = feat_array->from_constant("FEAT_PARTIAL_SKILL_AFFINITY_SEARCH");
    feat_partial_skill_affinity_spot = feat_array->from_constant("FEAT_PARTIAL_SKILL_AFFINITY_SPOT");
    feat_skill_affinity_move_silently = feat_array->from_constant("FEAT_SKILL_AFFINITY_MOVE_SILENTLY");
    feat_lucky = feat_array->from_constant("FEAT_LUCKY");
    feat_fearless = feat_array->from_constant("FEAT_FEARLESS");
    feat_good_aim = feat_array->from_constant("FEAT_GOOD_AIM");
    feat_uncanny_dodge_2 = feat_array->from_constant("FEAT_UNCANNY_DODGE_2");
    feat_uncanny_dodge_3 = feat_array->from_constant("FEAT_UNCANNY_DODGE_3");
    feat_uncanny_dodge_4 = feat_array->from_constant("FEAT_UNCANNY_DODGE_4");
    feat_uncanny_dodge_5 = feat_array->from_constant("FEAT_UNCANNY_DODGE_5");
    feat_uncanny_dodge_6 = feat_array->from_constant("FEAT_UNCANNY_DODGE_6");
    feat_weapon_proficiency_elf = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_ELF");
    feat_bard_songs = feat_array->from_constant("FEAT_BARD_SONGS");
    feat_quick_to_master = feat_array->from_constant("FEAT_QUICK_TO_MASTER");
    feat_slippery_mind = feat_array->from_constant("FEAT_SLIPPERY_MIND");
    feat_monk_ac_bonus = feat_array->from_constant("FEAT_MONK_AC_BONUS");
    feat_favored_enemy_dwarf = feat_array->from_constant("FEAT_FAVORED_ENEMY_DWARF");
    feat_favored_enemy_elf = feat_array->from_constant("FEAT_FAVORED_ENEMY_ELF");
    feat_favored_enemy_gnome = feat_array->from_constant("FEAT_FAVORED_ENEMY_GNOME");
    feat_favored_enemy_halfling = feat_array->from_constant("FEAT_FAVORED_ENEMY_HALFLING");
    feat_favored_enemy_halfelf = feat_array->from_constant("FEAT_FAVORED_ENEMY_HALFELF");
    feat_favored_enemy_halforc = feat_array->from_constant("FEAT_FAVORED_ENEMY_HALFORC");
    feat_favored_enemy_human = feat_array->from_constant("FEAT_FAVORED_ENEMY_HUMAN");
    feat_favored_enemy_aberration = feat_array->from_constant("FEAT_FAVORED_ENEMY_ABERRATION");
    feat_favored_enemy_animal = feat_array->from_constant("FEAT_FAVORED_ENEMY_ANIMAL");
    feat_favored_enemy_beast = feat_array->from_constant("FEAT_FAVORED_ENEMY_BEAST");
    feat_favored_enemy_construct = feat_array->from_constant("FEAT_FAVORED_ENEMY_CONSTRUCT");
    feat_favored_enemy_dragon = feat_array->from_constant("FEAT_FAVORED_ENEMY_DRAGON");
    feat_favored_enemy_goblinoid = feat_array->from_constant("FEAT_FAVORED_ENEMY_GOBLINOID");
    feat_favored_enemy_monstrous = feat_array->from_constant("FEAT_FAVORED_ENEMY_MONSTROUS");
    feat_favored_enemy_orc = feat_array->from_constant("FEAT_FAVORED_ENEMY_ORC");
    feat_favored_enemy_reptilian = feat_array->from_constant("FEAT_FAVORED_ENEMY_REPTILIAN");
    feat_favored_enemy_elemental = feat_array->from_constant("FEAT_FAVORED_ENEMY_ELEMENTAL");
    feat_favored_enemy_fey = feat_array->from_constant("FEAT_FAVORED_ENEMY_FEY");
    feat_favored_enemy_giant = feat_array->from_constant("FEAT_FAVORED_ENEMY_GIANT");
    feat_favored_enemy_magical_beast = feat_array->from_constant("FEAT_FAVORED_ENEMY_MAGICAL_BEAST");
    feat_favored_enemy_outsider = feat_array->from_constant("FEAT_FAVORED_ENEMY_OUTSIDER");
    feat_favored_enemy_shapechanger = feat_array->from_constant("FEAT_FAVORED_ENEMY_SHAPECHANGER");
    feat_favored_enemy_undead = feat_array->from_constant("FEAT_FAVORED_ENEMY_UNDEAD");
    feat_favored_enemy_vermin = feat_array->from_constant("FEAT_FAVORED_ENEMY_VERMIN");
    feat_weapon_proficiency_creature = feat_array->from_constant("FEAT_WEAPON_PROFICIENCY_CREATURE");
    feat_weapon_specialization_creature = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_CREATURE");
    feat_weapon_focus_creature = feat_array->from_constant("FEAT_WEAPON_FOCUS_CREATURE");
    feat_improved_critical_creature = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_CREATURE");
    feat_barbarian_rage = feat_array->from_constant("FEAT_BARBARIAN_RAGE");
    feat_turn_undead = feat_array->from_constant("FEAT_TURN_UNDEAD");
    feat_quivering_palm = feat_array->from_constant("FEAT_QUIVERING_PALM");
    feat_empty_body = feat_array->from_constant("FEAT_EMPTY_BODY");
    // feat_detect_evil = cr->add( "FEAT_DETECT_EVIL" , 298);
    feat_lay_on_hands = feat_array->from_constant("FEAT_LAY_ON_HANDS");
    feat_aura_of_courage = feat_array->from_constant("FEAT_AURA_OF_COURAGE");
    feat_smite_evil = feat_array->from_constant("FEAT_SMITE_EVIL");
    feat_remove_disease = feat_array->from_constant("FEAT_REMOVE_DISEASE");
    feat_summon_familiar = feat_array->from_constant("FEAT_SUMMON_FAMILIAR");
    feat_elemental_shape = feat_array->from_constant("FEAT_ELEMENTAL_SHAPE");
    feat_wild_shape = feat_array->from_constant("FEAT_WILD_SHAPE");
    feat_war_domain_power = feat_array->from_constant("FEAT_WAR_DOMAIN_POWER");
    feat_strength_domain_power = feat_array->from_constant("FEAT_STRENGTH_DOMAIN_POWER");
    feat_protection_domain_power = feat_array->from_constant("FEAT_PROTECTION_DOMAIN_POWER");
    feat_luck_domain_power = feat_array->from_constant("FEAT_LUCK_DOMAIN_POWER");
    feat_death_domain_power = feat_array->from_constant("FEAT_DEATH_DOMAIN_POWER");
    feat_air_domain_power = feat_array->from_constant("FEAT_AIR_DOMAIN_POWER");
    feat_animal_domain_power = feat_array->from_constant("FEAT_ANIMAL_DOMAIN_POWER");
    feat_destruction_domain_power = feat_array->from_constant("FEAT_DESTRUCTION_DOMAIN_POWER");
    feat_earth_domain_power = feat_array->from_constant("FEAT_EARTH_DOMAIN_POWER");
    feat_evil_domain_power = feat_array->from_constant("FEAT_EVIL_DOMAIN_POWER");
    feat_fire_domain_power = feat_array->from_constant("FEAT_FIRE_DOMAIN_POWER");
    feat_good_domain_power = feat_array->from_constant("FEAT_GOOD_DOMAIN_POWER");
    feat_healing_domain_power = feat_array->from_constant("FEAT_HEALING_DOMAIN_POWER");
    feat_knowledge_domain_power = feat_array->from_constant("FEAT_KNOWLEDGE_DOMAIN_POWER");
    feat_magic_domain_power = feat_array->from_constant("FEAT_MAGIC_DOMAIN_POWER");
    feat_plant_domain_power = feat_array->from_constant("FEAT_PLANT_DOMAIN_POWER");
    feat_sun_domain_power = feat_array->from_constant("FEAT_SUN_DOMAIN_POWER");
    feat_travel_domain_power = feat_array->from_constant("FEAT_TRAVEL_DOMAIN_POWER");
    feat_trickery_domain_power = feat_array->from_constant("FEAT_TRICKERY_DOMAIN_POWER");
    feat_water_domain_power = feat_array->from_constant("FEAT_WATER_DOMAIN_POWER");
    feat_lowlightvision = feat_array->from_constant("FEAT_LOWLIGHTVISION");
    feat_improved_initiative = feat_array->from_constant("FEAT_IMPROVED_INITIATIVE");
    feat_artist = feat_array->from_constant("FEAT_ARTIST");
    feat_blooded = feat_array->from_constant("FEAT_BLOODED");
    feat_bullheaded = feat_array->from_constant("FEAT_BULLHEADED");
    feat_courtly_magocracy = feat_array->from_constant("FEAT_COURTLY_MAGOCRACY");
    feat_luck_of_heroes = feat_array->from_constant("FEAT_LUCK_OF_HEROES");
    feat_resist_poison = feat_array->from_constant("FEAT_RESIST_POISON");
    feat_silver_palm = feat_array->from_constant("FEAT_SILVER_PALM");
    feat_snakeblood = feat_array->from_constant("FEAT_SNAKEBLOOD");
    feat_stealthy = feat_array->from_constant("FEAT_STEALTHY");
    feat_strongsoul = feat_array->from_constant("FEAT_STRONGSOUL");
    feat_expertise = feat_array->from_constant("FEAT_EXPERTISE");
    feat_improved_expertise = feat_array->from_constant("FEAT_IMPROVED_EXPERTISE");
    feat_great_cleave = feat_array->from_constant("FEAT_GREAT_CLEAVE");
    feat_spring_attack = feat_array->from_constant("FEAT_SPRING_ATTACK");
    feat_greater_spell_focus_abjuration = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_ABJURATION");
    feat_greater_spell_focus_conjuration = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_CONJURATION");
    feat_greater_spell_focus_diviniation = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_DIVINIATION");
    feat_greater_spell_focus_divination = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_DIVINATION");
    feat_greater_spell_focus_enchantment = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_ENCHANTMENT");
    feat_greater_spell_focus_evocation = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_EVOCATION");
    feat_greater_spell_focus_illusion = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_ILLUSION");
    feat_greater_spell_focus_necromancy = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_NECROMANCY");
    feat_greater_spell_focus_transmutation = feat_array->from_constant("FEAT_GREATER_SPELL_FOCUS_TRANSMUTATION");
    feat_greater_spell_penetration = feat_array->from_constant("FEAT_GREATER_SPELL_PENETRATION");
    feat_thug = feat_array->from_constant("FEAT_THUG");
    feat_skillfocus_appraise = feat_array->from_constant("FEAT_SKILLFOCUS_APPRAISE");
    feat_skill_focus_tumble = feat_array->from_constant("FEAT_SKILL_FOCUS_TUMBLE");
    feat_skill_focus_craft_trap = feat_array->from_constant("FEAT_SKILL_FOCUS_CRAFT_TRAP");
    feat_blind_fight = feat_array->from_constant("FEAT_BLIND_FIGHT");
    feat_circle_kick = feat_array->from_constant("FEAT_CIRCLE_KICK");
    feat_extra_stunning_attack = feat_array->from_constant("FEAT_EXTRA_STUNNING_ATTACK");
    feat_rapid_reload = feat_array->from_constant("FEAT_RAPID_RELOAD");
    feat_zen_archery = feat_array->from_constant("FEAT_ZEN_ARCHERY");
    feat_divine_might = feat_array->from_constant("FEAT_DIVINE_MIGHT");
    feat_divine_shield = feat_array->from_constant("FEAT_DIVINE_SHIELD");
    feat_arcane_defense_abjuration = feat_array->from_constant("FEAT_ARCANE_DEFENSE_ABJURATION");
    feat_arcane_defense_conjuration = feat_array->from_constant("FEAT_ARCANE_DEFENSE_CONJURATION");
    feat_arcane_defense_divination = feat_array->from_constant("FEAT_ARCANE_DEFENSE_DIVINATION");
    feat_arcane_defense_enchantment = feat_array->from_constant("FEAT_ARCANE_DEFENSE_ENCHANTMENT");
    feat_arcane_defense_evocation = feat_array->from_constant("FEAT_ARCANE_DEFENSE_EVOCATION");
    feat_arcane_defense_illusion = feat_array->from_constant("FEAT_ARCANE_DEFENSE_ILLUSION");
    feat_arcane_defense_necromancy = feat_array->from_constant("FEAT_ARCANE_DEFENSE_NECROMANCY");
    feat_arcane_defense_transmutation = feat_array->from_constant("FEAT_ARCANE_DEFENSE_TRANSMUTATION");
    feat_extra_music = feat_array->from_constant("FEAT_EXTRA_MUSIC");
    feat_lingering_song = feat_array->from_constant("FEAT_LINGERING_SONG");
    feat_dirty_fighting = feat_array->from_constant("FEAT_DIRTY_FIGHTING");
    feat_resist_disease = feat_array->from_constant("FEAT_RESIST_DISEASE");
    feat_resist_energy_cold = feat_array->from_constant("FEAT_RESIST_ENERGY_COLD");
    feat_resist_energy_acid = feat_array->from_constant("FEAT_RESIST_ENERGY_ACID");
    feat_resist_energy_fire = feat_array->from_constant("FEAT_RESIST_ENERGY_FIRE");
    feat_resist_energy_electrical = feat_array->from_constant("FEAT_RESIST_ENERGY_ELECTRICAL");
    feat_resist_energy_sonic = feat_array->from_constant("FEAT_RESIST_ENERGY_SONIC");
    feat_hide_in_plain_sight = feat_array->from_constant("FEAT_HIDE_IN_PLAIN_SIGHT");
    feat_shadow_daze = feat_array->from_constant("FEAT_SHADOW_DAZE");
    feat_summon_shadow = feat_array->from_constant("FEAT_SUMMON_SHADOW");
    feat_shadow_evade = feat_array->from_constant("FEAT_SHADOW_EVADE");
    feat_deneirs_eye = feat_array->from_constant("FEAT_DENEIRS_EYE");
    feat_tymoras_smile = feat_array->from_constant("FEAT_TYMORAS_SMILE");
    feat_lliiras_heart = feat_array->from_constant("FEAT_LLIIRAS_HEART");
    feat_craft_harper_item = feat_array->from_constant("FEAT_CRAFT_HARPER_ITEM");
    feat_harper_sleep = feat_array->from_constant("FEAT_HARPER_SLEEP");
    feat_harper_cats_grace = feat_array->from_constant("FEAT_HARPER_CATS_GRACE");
    feat_harper_eagles_splendor = feat_array->from_constant("FEAT_HARPER_EAGLES_SPLENDOR");
    feat_harper_invisibility = feat_array->from_constant("FEAT_HARPER_INVISIBILITY");

    feat_prestige_enchant_arrow_1 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_1");

    feat_prestige_enchant_arrow_2 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_2");
    feat_prestige_enchant_arrow_3 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_3");
    feat_prestige_enchant_arrow_4 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_4");
    feat_prestige_enchant_arrow_5 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_5");
    feat_prestige_imbue_arrow = feat_array->from_constant("FEAT_PRESTIGE_IMBUE_ARROW");
    feat_prestige_seeker_arrow_1 = feat_array->from_constant("FEAT_PRESTIGE_SEEKER_ARROW_1");
    feat_prestige_seeker_arrow_2 = feat_array->from_constant("FEAT_PRESTIGE_SEEKER_ARROW_2");
    feat_prestige_hail_of_arrows = feat_array->from_constant("FEAT_PRESTIGE_HAIL_OF_ARROWS");
    feat_prestige_arrow_of_death = feat_array->from_constant("FEAT_PRESTIGE_ARROW_OF_DEATH");

    feat_prestige_death_attack_1 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_1");
    feat_prestige_death_attack_2 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_2");
    feat_prestige_death_attack_3 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_3");
    feat_prestige_death_attack_4 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_4");
    feat_prestige_death_attack_5 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_5");

    feat_blackguard_sneak_attack_1d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_1D6");
    feat_blackguard_sneak_attack_2d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_2D6");
    feat_blackguard_sneak_attack_3d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_3D6");

    feat_prestige_poison_save_1 = feat_array->from_constant("FEAT_PRESTIGE_POISON_SAVE_1");
    feat_prestige_poison_save_2 = feat_array->from_constant("FEAT_PRESTIGE_POISON_SAVE_2");
    feat_prestige_poison_save_3 = feat_array->from_constant("FEAT_PRESTIGE_POISON_SAVE_3");
    feat_prestige_poison_save_4 = feat_array->from_constant("FEAT_PRESTIGE_POISON_SAVE_4");
    feat_prestige_poison_save_5 = feat_array->from_constant("FEAT_PRESTIGE_POISON_SAVE_5");

    feat_prestige_spell_ghostly_visage = feat_array->from_constant("FEAT_PRESTIGE_SPELL_GHOSTLY_VISAGE");
    feat_prestige_darkness = feat_array->from_constant("FEAT_PRESTIGE_DARKNESS");
    feat_prestige_invisibility_1 = feat_array->from_constant("FEAT_PRESTIGE_INVISIBILITY_1");
    feat_prestige_invisibility_2 = feat_array->from_constant("FEAT_PRESTIGE_INVISIBILITY_2");

    feat_smite_good = feat_array->from_constant("FEAT_SMITE_GOOD");

    feat_prestige_dark_blessing = feat_array->from_constant("FEAT_PRESTIGE_DARK_BLESSING");
    feat_inflict_light_wounds = feat_array->from_constant("FEAT_INFLICT_LIGHT_WOUNDS");
    feat_inflict_moderate_wounds = feat_array->from_constant("FEAT_INFLICT_MODERATE_WOUNDS");
    feat_inflict_serious_wounds = feat_array->from_constant("FEAT_INFLICT_SERIOUS_WOUNDS");
    feat_inflict_critical_wounds = feat_array->from_constant("FEAT_INFLICT_CRITICAL_WOUNDS");
    feat_bulls_strength = feat_array->from_constant("FEAT_BULLS_STRENGTH");
    feat_contagion = feat_array->from_constant("FEAT_CONTAGION");
    feat_eye_of_gruumsh_blinding_spittle = feat_array->from_constant("FEAT_EYE_OF_GRUUMSH_BLINDING_SPITTLE");
    feat_eye_of_gruumsh_blinding_spittle_2 = feat_array->from_constant("FEAT_EYE_OF_GRUUMSH_BLINDING_SPITTLE_2");
    feat_eye_of_gruumsh_command_the_horde = feat_array->from_constant("FEAT_EYE_OF_GRUUMSH_COMMAND_THE_HORDE");
    feat_eye_of_gruumsh_swing_blindly = feat_array->from_constant("FEAT_EYE_OF_GRUUMSH_SWING_BLINDLY");
    feat_eye_of_gruumsh_ritual_scarring = feat_array->from_constant("FEAT_EYE_OF_GRUUMSH_RITUAL_SCARRING");
    feat_blindsight_5_feet = feat_array->from_constant("FEAT_BLINDSIGHT_5_FEET");
    feat_blindsight_10_feet = feat_array->from_constant("FEAT_BLINDSIGHT_10_FEET");
    feat_eye_of_gruumsh_sight_of_gruumsh = feat_array->from_constant("FEAT_EYE_OF_GRUUMSH_SIGHT_OF_GRUUMSH");
    feat_blindsight_60_feet = feat_array->from_constant("FEAT_BLINDSIGHT_60_FEET");
    feat_shou_disciple_dodge_2 = feat_array->from_constant("FEAT_SHOU_DISCIPLE_DODGE_2");
    feat_epic_armor_skin = feat_array->from_constant("FEAT_EPIC_ARMOR_SKIN");
    feat_epic_blinding_speed = feat_array->from_constant("FEAT_EPIC_BLINDING_SPEED");
    feat_epic_damage_reduction_3 = feat_array->from_constant("FEAT_EPIC_DAMAGE_REDUCTION_3");
    feat_epic_damage_reduction_6 = feat_array->from_constant("FEAT_EPIC_DAMAGE_REDUCTION_6");
    feat_epic_damage_reduction_9 = feat_array->from_constant("FEAT_EPIC_DAMAGE_REDUCTION_9");
    feat_epic_devastating_critical_club = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_CLUB");
    feat_epic_devastating_critical_dagger = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_DAGGER");
    feat_epic_devastating_critical_dart = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_DART");
    feat_epic_devastating_critical_heavycrossbow = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_HEAVYCROSSBOW");
    feat_epic_devastating_critical_lightcrossbow = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTCROSSBOW");
    feat_epic_devastating_critical_lightmace = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTMACE");
    feat_epic_devastating_critical_morningstar = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_MORNINGSTAR");
    feat_epic_devastating_critical_quarterstaff = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_QUARTERSTAFF");
    feat_epic_devastating_critical_shortspear = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SHORTSPEAR");
    feat_epic_devastating_critical_sickle = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SICKLE");
    feat_epic_devastating_critical_sling = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SLING");
    feat_epic_devastating_critical_unarmed = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_UNARMED");
    feat_epic_devastating_critical_longbow = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_LONGBOW");
    feat_epic_devastating_critical_shortbow = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SHORTBOW");
    feat_epic_devastating_critical_shortsword = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SHORTSWORD");
    feat_epic_devastating_critical_rapier = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_RAPIER");
    feat_epic_devastating_critical_scimitar = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SCIMITAR");
    feat_epic_devastating_critical_longsword = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_LONGSWORD");
    feat_epic_devastating_critical_greatsword = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_GREATSWORD");
    feat_epic_devastating_critical_handaxe = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_HANDAXE");
    feat_epic_devastating_critical_throwingaxe = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_THROWINGAXE");
    feat_epic_devastating_critical_battleaxe = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_BATTLEAXE");
    feat_epic_devastating_critical_greataxe = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_GREATAXE");
    feat_epic_devastating_critical_halberd = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_HALBERD");
    feat_epic_devastating_critical_lighthammer = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTHAMMER");
    feat_epic_devastating_critical_lightflail = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTFLAIL");
    feat_epic_devastating_critical_warhammer = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_WARHAMMER");
    feat_epic_devastating_critical_heavyflail = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_HEAVYFLAIL");
    feat_epic_devastating_critical_kama = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_KAMA");
    feat_epic_devastating_critical_kukri = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_KUKRI");
    feat_epic_devastating_critical_shuriken = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SHURIKEN");
    feat_epic_devastating_critical_scythe = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_SCYTHE");
    feat_epic_devastating_critical_katana = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_KATANA");
    feat_epic_devastating_critical_bastardsword = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_BASTARDSWORD");
    feat_epic_devastating_critical_diremace = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_DIREMACE");
    feat_epic_devastating_critical_doubleaxe = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_DOUBLEAXE");
    feat_epic_devastating_critical_twobladedsword = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_TWOBLADEDSWORD");
    feat_epic_devastating_critical_creature = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_CREATURE");
    feat_epic_energy_resistance_cold_1 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_1");
    feat_epic_energy_resistance_cold_2 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_2");
    feat_epic_energy_resistance_cold_3 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_3");
    feat_epic_energy_resistance_cold_4 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_4");
    feat_epic_energy_resistance_cold_5 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_5");
    feat_epic_energy_resistance_cold_6 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_6");
    feat_epic_energy_resistance_cold_7 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_7");
    feat_epic_energy_resistance_cold_8 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_8");
    feat_epic_energy_resistance_cold_9 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_9");
    feat_epic_energy_resistance_cold_10 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_COLD_10");
    feat_epic_energy_resistance_acid_1 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_1");
    feat_epic_energy_resistance_acid_2 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_2");
    feat_epic_energy_resistance_acid_3 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_3");
    feat_epic_energy_resistance_acid_4 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_4");
    feat_epic_energy_resistance_acid_5 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_5");
    feat_epic_energy_resistance_acid_6 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_6");
    feat_epic_energy_resistance_acid_7 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_7");
    feat_epic_energy_resistance_acid_8 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_8");
    feat_epic_energy_resistance_acid_9 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_9");
    feat_epic_energy_resistance_acid_10 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ACID_10");
    feat_epic_energy_resistance_fire_1 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_1");
    feat_epic_energy_resistance_fire_2 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_2");
    feat_epic_energy_resistance_fire_3 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_3");
    feat_epic_energy_resistance_fire_4 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_4");
    feat_epic_energy_resistance_fire_5 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_5");
    feat_epic_energy_resistance_fire_6 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_6");
    feat_epic_energy_resistance_fire_7 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_7");
    feat_epic_energy_resistance_fire_8 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_8");
    feat_epic_energy_resistance_fire_9 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_9");
    feat_epic_energy_resistance_fire_10 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_10");
    feat_epic_energy_resistance_electrical_1 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_1");
    feat_epic_energy_resistance_electrical_2 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_2");
    feat_epic_energy_resistance_electrical_3 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_3");
    feat_epic_energy_resistance_electrical_4 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_4");
    feat_epic_energy_resistance_electrical_5 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_5");
    feat_epic_energy_resistance_electrical_6 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_6");
    feat_epic_energy_resistance_electrical_7 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_7");
    feat_epic_energy_resistance_electrical_8 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_8");
    feat_epic_energy_resistance_electrical_9 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_9");
    feat_epic_energy_resistance_electrical_10 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_10");
    feat_epic_energy_resistance_sonic_1 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_1");
    feat_epic_energy_resistance_sonic_2 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_2");
    feat_epic_energy_resistance_sonic_3 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_3");
    feat_epic_energy_resistance_sonic_4 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_4");
    feat_epic_energy_resistance_sonic_5 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_5");
    feat_epic_energy_resistance_sonic_6 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_6");
    feat_epic_energy_resistance_sonic_7 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_7");
    feat_epic_energy_resistance_sonic_8 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_8");
    feat_epic_energy_resistance_sonic_9 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_9");
    feat_epic_energy_resistance_sonic_10 = feat_array->from_constant("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_10");

    feat_epic_fortitude = feat_array->from_constant("FEAT_EPIC_FORTITUDE");
    feat_epic_prowess = feat_array->from_constant("FEAT_EPIC_PROWESS");
    feat_epic_reflexes = feat_array->from_constant("FEAT_EPIC_REFLEXES");
    feat_epic_reputation = feat_array->from_constant("FEAT_EPIC_REPUTATION");
    feat_epic_skill_focus_animal_empathy = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_ANIMAL_EMPATHY");
    feat_epic_skill_focus_appraise = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_APPRAISE");
    feat_epic_skill_focus_concentration = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_CONCENTRATION");
    feat_epic_skill_focus_craft_trap = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_CRAFT_TRAP");
    feat_epic_skill_focus_disable_trap = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_DISABLETRAP");
    feat_epic_skill_focus_discipline = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_DISCIPLINE");
    feat_epic_skill_focus_heal = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_HEAL");
    feat_epic_skill_focus_hide = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_HIDE");
    feat_epic_skill_focus_listen = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_LISTEN");
    feat_epic_skill_focus_lore = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_LORE");
    feat_epic_skill_focus_move_silently = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_MOVESILENTLY");
    feat_epic_skill_focus_open_lock = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_OPENLOCK");
    feat_epic_skill_focus_parry = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_PARRY");
    feat_epic_skill_focus_perform = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_PERFORM");
    feat_epic_skill_focus_persuade = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_PERSUADE");
    feat_epic_skill_focus_pick_pocket = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_PICKPOCKET");
    feat_epic_skill_focus_search = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_SEARCH");
    feat_epic_skill_focus_set_trap = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_SETTRAP");
    feat_epic_skill_focus_spellcraft = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_SPELLCRAFT");
    feat_epic_skill_focus_spot = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_SPOT");
    feat_epic_skill_focus_taunt = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_TAUNT");
    feat_epic_skill_focus_tumble = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_TUMBLE");
    feat_epic_skill_focus_use_magic_device = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_USEMAGICDEVICE");
    feat_epic_spell_focus_abjuration = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_ABJURATION");
    feat_epic_spell_focus_conjuration = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_CONJURATION");
    feat_epic_spell_focus_divination = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_DIVINATION");
    feat_epic_spell_focus_enchantment = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_ENCHANTMENT");
    feat_epic_spell_focus_evocation = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_EVOCATION");
    feat_epic_spell_focus_illusion = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_ILLUSION");
    feat_epic_spell_focus_necromancy = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_NECROMANCY");
    feat_epic_spell_focus_transmutation = feat_array->from_constant("FEAT_EPIC_SPELL_FOCUS_TRANSMUTATION");
    feat_epic_spell_penetration = feat_array->from_constant("FEAT_EPIC_SPELL_PENETRATION");
    feat_epic_weapon_focus_club = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_CLUB");
    feat_epic_weapon_focus_dagger = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_DAGGER");
    feat_epic_weapon_focus_dart = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_DART");
    feat_epic_weapon_focus_heavycrossbow = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_HEAVYCROSSBOW");
    feat_epic_weapon_focus_lightcrossbow = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_LIGHTCROSSBOW");
    feat_epic_weapon_focus_lightmace = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_LIGHTMACE");
    feat_epic_weapon_focus_morningstar = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_MORNINGSTAR");
    feat_epic_weapon_focus_quarterstaff = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_QUARTERSTAFF");
    feat_epic_weapon_focus_shortspear = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SHORTSPEAR");
    feat_epic_weapon_focus_sickle = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SICKLE");
    feat_epic_weapon_focus_sling = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SLING");
    feat_epic_weapon_focus_unarmed = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_UNARMED");
    feat_epic_weapon_focus_longbow = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_LONGBOW");
    feat_epic_weapon_focus_shortbow = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SHORTBOW");
    feat_epic_weapon_focus_shortsword = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SHORTSWORD");
    feat_epic_weapon_focus_rapier = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_RAPIER");
    feat_epic_weapon_focus_scimitar = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SCIMITAR");
    feat_epic_weapon_focus_longsword = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_LONGSWORD");
    feat_epic_weapon_focus_greatsword = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_GREATSWORD");
    feat_epic_weapon_focus_handaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_HANDAXE");
    feat_epic_weapon_focus_throwingaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_THROWINGAXE");
    feat_epic_weapon_focus_battleaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_BATTLEAXE");
    feat_epic_weapon_focus_greataxe = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_GREATAXE");
    feat_epic_weapon_focus_halberd = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_HALBERD");
    feat_epic_weapon_focus_lighthammer = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_LIGHTHAMMER");
    feat_epic_weapon_focus_lightflail = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_LIGHTFLAIL");
    feat_epic_weapon_focus_warhammer = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_WARHAMMER");
    feat_epic_weapon_focus_heavyflail = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_HEAVYFLAIL");
    feat_epic_weapon_focus_kama = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_KAMA");
    feat_epic_weapon_focus_kukri = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_KUKRI");
    feat_epic_weapon_focus_shuriken = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SHURIKEN");
    feat_epic_weapon_focus_scythe = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_SCYTHE");
    feat_epic_weapon_focus_katana = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_KATANA");
    feat_epic_weapon_focus_bastardsword = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_BASTARDSWORD");
    feat_epic_weapon_focus_diremace = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_DIREMACE");
    feat_epic_weapon_focus_doubleaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_DOUBLEAXE");
    feat_epic_weapon_focus_twobladedsword = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_TWOBLADEDSWORD");
    feat_epic_weapon_focus_creature = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_CREATURE");
    feat_epic_weapon_specialization_club = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_CLUB");
    feat_epic_weapon_specialization_dagger = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_DAGGER");
    feat_epic_weapon_specialization_dart = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_DART");
    feat_epic_weapon_specialization_heavycrossbow = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_HEAVYCROSSBOW");
    feat_epic_weapon_specialization_lightcrossbow = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTCROSSBOW");
    feat_epic_weapon_specialization_lightmace = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTMACE");
    feat_epic_weapon_specialization_morningstar = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_MORNINGSTAR");
    feat_epic_weapon_specialization_quarterstaff = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_QUARTERSTAFF");
    feat_epic_weapon_specialization_shortspear = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SHORTSPEAR");
    feat_epic_weapon_specialization_sickle = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SICKLE");
    feat_epic_weapon_specialization_sling = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SLING");
    feat_epic_weapon_specialization_unarmed = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_UNARMED");
    feat_epic_weapon_specialization_longbow = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_LONGBOW");
    feat_epic_weapon_specialization_shortbow = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SHORTBOW");
    feat_epic_weapon_specialization_shortsword = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SHORTSWORD");
    feat_epic_weapon_specialization_rapier = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_RAPIER");
    feat_epic_weapon_specialization_scimitar = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SCIMITAR");
    feat_epic_weapon_specialization_longsword = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_LONGSWORD");
    feat_epic_weapon_specialization_greatsword = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_GREATSWORD");
    feat_epic_weapon_specialization_handaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_HANDAXE");
    feat_epic_weapon_specialization_throwingaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_THROWINGAXE");
    feat_epic_weapon_specialization_battleaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_BATTLEAXE");
    feat_epic_weapon_specialization_greataxe = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_GREATAXE");
    feat_epic_weapon_specialization_halberd = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_HALBERD");
    feat_epic_weapon_specialization_lighthammer = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTHAMMER");
    feat_epic_weapon_specialization_lightflail = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTFLAIL");
    feat_epic_weapon_specialization_warhammer = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_WARHAMMER");
    feat_epic_weapon_specialization_heavyflail = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_HEAVYFLAIL");
    feat_epic_weapon_specialization_kama = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_KAMA");
    feat_epic_weapon_specialization_kukri = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_KUKRI");
    feat_epic_weapon_specialization_shuriken = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SHURIKEN");
    feat_epic_weapon_specialization_scythe = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_SCYTHE");
    feat_epic_weapon_specialization_katana = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_KATANA");
    feat_epic_weapon_specialization_bastardsword = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_BASTARDSWORD");
    feat_epic_weapon_specialization_diremace = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_DIREMACE");
    feat_epic_weapon_specialization_doubleaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_DOUBLEAXE");
    feat_epic_weapon_specialization_twobladedsword = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_TWOBLADEDSWORD");
    feat_epic_weapon_specialization_creature = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_CREATURE");

    feat_epic_will = feat_array->from_constant("FEAT_EPIC_WILL");
    feat_epic_improved_combat_casting = feat_array->from_constant("FEAT_EPIC_IMPROVED_COMBAT_CASTING");
    feat_epic_improved_ki_strike_4 = feat_array->from_constant("FEAT_EPIC_IMPROVED_KI_STRIKE_4");
    feat_epic_improved_ki_strike_5 = feat_array->from_constant("FEAT_EPIC_IMPROVED_KI_STRIKE_5");
    feat_epic_improved_spell_resistance_1 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_1");
    feat_epic_improved_spell_resistance_2 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_2");
    feat_epic_improved_spell_resistance_3 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_3");
    feat_epic_improved_spell_resistance_4 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_4");
    feat_epic_improved_spell_resistance_5 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_5");
    feat_epic_improved_spell_resistance_6 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_6");
    feat_epic_improved_spell_resistance_7 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_7");
    feat_epic_improved_spell_resistance_8 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_8");
    feat_epic_improved_spell_resistance_9 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_9");
    feat_epic_improved_spell_resistance_10 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_10");
    feat_epic_overwhelming_critical_club = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_CLUB");
    feat_epic_overwhelming_critical_dagger = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_DAGGER");
    feat_epic_overwhelming_critical_dart = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_DART");
    feat_epic_overwhelming_critical_heavycrossbow = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_HEAVYCROSSBOW");
    feat_epic_overwhelming_critical_lightcrossbow = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTCROSSBOW");
    feat_epic_overwhelming_critical_lightmace = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTMACE");
    feat_epic_overwhelming_critical_morningstar = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_MORNINGSTAR");
    feat_epic_overwhelming_critical_quarterstaff = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_QUARTERSTAFF");
    feat_epic_overwhelming_critical_shortspear = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SHORTSPEAR");
    feat_epic_overwhelming_critical_sickle = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SICKLE");
    feat_epic_overwhelming_critical_sling = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SLING");
    feat_epic_overwhelming_critical_unarmed = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_UNARMED");
    feat_epic_overwhelming_critical_longbow = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_LONGBOW");
    feat_epic_overwhelming_critical_shortbow = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SHORTBOW");
    feat_epic_overwhelming_critical_shortsword = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SHORTSWORD");
    feat_epic_overwhelming_critical_rapier = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_RAPIER");
    feat_epic_overwhelming_critical_scimitar = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SCIMITAR");
    feat_epic_overwhelming_critical_longsword = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_LONGSWORD");
    feat_epic_overwhelming_critical_greatsword = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_GREATSWORD");
    feat_epic_overwhelming_critical_handaxe = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_HANDAXE");
    feat_epic_overwhelming_critical_throwingaxe = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_THROWINGAXE");
    feat_epic_overwhelming_critical_battleaxe = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_BATTLEAXE");
    feat_epic_overwhelming_critical_greataxe = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_GREATAXE");
    feat_epic_overwhelming_critical_halberd = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_HALBERD");
    feat_epic_overwhelming_critical_lighthammer = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTHAMMER");
    feat_epic_overwhelming_critical_lightflail = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTFLAIL");
    feat_epic_overwhelming_critical_warhammer = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_WARHAMMER");
    feat_epic_overwhelming_critical_heavyflail = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_HEAVYFLAIL");
    feat_epic_overwhelming_critical_kama = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_KAMA");
    feat_epic_overwhelming_critical_kukri = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_KUKRI");
    feat_epic_overwhelming_critical_shuriken = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SHURIKEN");
    feat_epic_overwhelming_critical_scythe = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_SCYTHE");
    feat_epic_overwhelming_critical_katana = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_KATANA");
    feat_epic_overwhelming_critical_bastardsword = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_BASTARDSWORD");
    feat_epic_overwhelming_critical_diremace = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_DIREMACE");
    feat_epic_overwhelming_critical_doubleaxe = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_DOUBLEAXE");
    feat_epic_overwhelming_critical_twobladedsword = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_TWOBLADEDSWORD");
    feat_epic_overwhelming_critical_creature = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_CREATURE");
    feat_epic_perfect_health = feat_array->from_constant("FEAT_EPIC_PERFECT_HEALTH");
    feat_epic_self_concealment_10 = feat_array->from_constant("FEAT_EPIC_SELF_CONCEALMENT_10");
    feat_epic_self_concealment_20 = feat_array->from_constant("FEAT_EPIC_SELF_CONCEALMENT_20");
    feat_epic_self_concealment_30 = feat_array->from_constant("FEAT_EPIC_SELF_CONCEALMENT_30");
    feat_epic_self_concealment_40 = feat_array->from_constant("FEAT_EPIC_SELF_CONCEALMENT_40");
    feat_epic_self_concealment_50 = feat_array->from_constant("FEAT_EPIC_SELF_CONCEALMENT_50");
    feat_epic_superior_initiative = feat_array->from_constant("FEAT_EPIC_SUPERIOR_INITIATIVE");
    feat_epic_toughness_1 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_1");
    feat_epic_toughness_2 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_2");
    feat_epic_toughness_3 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_3");
    feat_epic_toughness_4 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_4");
    feat_epic_toughness_5 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_5");
    feat_epic_toughness_6 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_6");
    feat_epic_toughness_7 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_7");
    feat_epic_toughness_8 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_8");
    feat_epic_toughness_9 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_9");
    feat_epic_toughness_10 = feat_array->from_constant("FEAT_EPIC_TOUGHNESS_10");
    feat_epic_great_charisma_1 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_1");
    feat_epic_great_charisma_2 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_2");
    feat_epic_great_charisma_3 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_3");
    feat_epic_great_charisma_4 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_4");
    feat_epic_great_charisma_5 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_5");
    feat_epic_great_charisma_6 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_6");
    feat_epic_great_charisma_7 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_7");
    feat_epic_great_charisma_8 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_8");
    feat_epic_great_charisma_9 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_9");
    feat_epic_great_charisma_10 = feat_array->from_constant("FEAT_EPIC_GREAT_CHARISMA_10");
    feat_epic_great_constitution_1 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_1");
    feat_epic_great_constitution_2 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_2");
    feat_epic_great_constitution_3 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_3");
    feat_epic_great_constitution_4 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_4");
    feat_epic_great_constitution_5 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_5");
    feat_epic_great_constitution_6 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_6");
    feat_epic_great_constitution_7 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_7");
    feat_epic_great_constitution_8 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_8");
    feat_epic_great_constitution_9 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_9");
    feat_epic_great_constitution_10 = feat_array->from_constant("FEAT_EPIC_GREAT_CONSTITUTION_10");
    feat_epic_great_dexterity_1 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_1");
    feat_epic_great_dexterity_2 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_2");
    feat_epic_great_dexterity_3 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_3");
    feat_epic_great_dexterity_4 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_4");
    feat_epic_great_dexterity_5 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_5");
    feat_epic_great_dexterity_6 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_6");
    feat_epic_great_dexterity_7 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_7");
    feat_epic_great_dexterity_8 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_8");
    feat_epic_great_dexterity_9 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_9");
    feat_epic_great_dexterity_10 = feat_array->from_constant("FEAT_EPIC_GREAT_DEXTERITY_10");
    feat_epic_great_intelligence_1 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_1");
    feat_epic_great_intelligence_2 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_2");
    feat_epic_great_intelligence_3 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_3");
    feat_epic_great_intelligence_4 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_4");
    feat_epic_great_intelligence_5 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_5");
    feat_epic_great_intelligence_6 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_6");
    feat_epic_great_intelligence_7 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_7");
    feat_epic_great_intelligence_8 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_8");
    feat_epic_great_intelligence_9 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_9");
    feat_epic_great_intelligence_10 = feat_array->from_constant("FEAT_EPIC_GREAT_INTELLIGENCE_10");
    feat_epic_great_wisdom_1 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_1");
    feat_epic_great_wisdom_2 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_2");
    feat_epic_great_wisdom_3 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_3");
    feat_epic_great_wisdom_4 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_4");
    feat_epic_great_wisdom_5 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_5");
    feat_epic_great_wisdom_6 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_6");
    feat_epic_great_wisdom_7 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_7");
    feat_epic_great_wisdom_8 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_8");
    feat_epic_great_wisdom_9 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_9");
    feat_epic_great_wisdom_10 = feat_array->from_constant("FEAT_EPIC_GREAT_WISDOM_10");
    feat_epic_great_strength_1 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_1");
    feat_epic_great_strength_2 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_2");
    feat_epic_great_strength_3 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_3");
    feat_epic_great_strength_4 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_4");
    feat_epic_great_strength_5 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_5");
    feat_epic_great_strength_6 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_6");
    feat_epic_great_strength_7 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_7");
    feat_epic_great_strength_8 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_8");
    feat_epic_great_strength_9 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_9");
    feat_epic_great_strength_10 = feat_array->from_constant("FEAT_EPIC_GREAT_STRENGTH_10");
    feat_epic_great_smiting_1 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_1");
    feat_epic_great_smiting_2 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_2");
    feat_epic_great_smiting_3 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_3");
    feat_epic_great_smiting_4 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_4");
    feat_epic_great_smiting_5 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_5");
    feat_epic_great_smiting_6 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_6");
    feat_epic_great_smiting_7 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_7");
    feat_epic_great_smiting_8 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_8");
    feat_epic_great_smiting_9 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_9");
    feat_epic_great_smiting_10 = feat_array->from_constant("FEAT_EPIC_GREAT_SMITING_10");
    feat_epic_improved_sneak_attack_1 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_1");
    feat_epic_improved_sneak_attack_2 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_2");
    feat_epic_improved_sneak_attack_3 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_3");
    feat_epic_improved_sneak_attack_4 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_4");
    feat_epic_improved_sneak_attack_5 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_5");
    feat_epic_improved_sneak_attack_6 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_6");
    feat_epic_improved_sneak_attack_7 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_7");
    feat_epic_improved_sneak_attack_8 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_8");
    feat_epic_improved_sneak_attack_9 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_9");
    feat_epic_improved_sneak_attack_10 = feat_array->from_constant("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_10");
    feat_epic_improved_stunning_fist_1 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_1");
    feat_epic_improved_stunning_fist_2 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_2");
    feat_epic_improved_stunning_fist_3 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_3");
    feat_epic_improved_stunning_fist_4 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_4");
    feat_epic_improved_stunning_fist_5 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_5");
    feat_epic_improved_stunning_fist_6 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_6");
    feat_epic_improved_stunning_fist_7 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_7");
    feat_epic_improved_stunning_fist_8 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_8");
    feat_epic_improved_stunning_fist_9 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_9");
    feat_epic_improved_stunning_fist_10 = feat_array->from_constant("FEAT_EPIC_IMPROVED_STUNNING_FIST_10");

    // feat_epic_planar_turning = cr->add( "FEAT_EPIC_PLANAR_TURNING"     ,  854);
    feat_epic_bane_of_enemies = feat_array->from_constant("FEAT_EPIC_BANE_OF_ENEMIES");
    feat_epic_dodge = feat_array->from_constant("FEAT_EPIC_DODGE");
    feat_epic_automatic_quicken_1 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_QUICKEN_1");
    feat_epic_automatic_quicken_2 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_QUICKEN_2");
    feat_epic_automatic_quicken_3 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_QUICKEN_3");
    feat_epic_automatic_silent_spell_1 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_SILENT_SPELL_1");
    feat_epic_automatic_silent_spell_2 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_SILENT_SPELL_2");
    feat_epic_automatic_silent_spell_3 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_SILENT_SPELL_3");
    feat_epic_automatic_still_spell_1 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_STILL_SPELL_1");
    feat_epic_automatic_still_spell_2 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_STILL_SPELL_2");
    feat_epic_automatic_still_spell_3 = feat_array->from_constant("FEAT_EPIC_AUTOMATIC_STILL_SPELL_3");

    feat_shou_disciple_martial_flurry_light = feat_array->from_constant("FEAT_SHOU_DISCIPLE_MARTIAL_FLURRY_LIGHT");
    feat_whirlwind_attack = feat_array->from_constant("FEAT_WHIRLWIND_ATTACK");
    feat_improved_whirlwind = feat_array->from_constant("FEAT_IMPROVED_WHIRLWIND");
    feat_mighty_rage = feat_array->from_constant("FEAT_MIGHTY_RAGE");
    feat_epic_lasting_inspiration = feat_array->from_constant("FEAT_EPIC_LASTING_INSPIRATION");
    feat_curse_song = feat_array->from_constant("FEAT_CURSE_SONG");
    feat_epic_wild_shape_undead = feat_array->from_constant("FEAT_EPIC_WILD_SHAPE_UNDEAD");
    feat_epic_wild_shape_dragon = feat_array->from_constant("FEAT_EPIC_WILD_SHAPE_DRAGON");
    feat_epic_spell_mummy_dust = feat_array->from_constant("FEAT_EPIC_SPELL_MUMMY_DUST");
    feat_epic_spell_dragon_knight = feat_array->from_constant("FEAT_EPIC_SPELL_DRAGON_KNIGHT");
    feat_epic_spell_hellball = feat_array->from_constant("FEAT_EPIC_SPELL_HELLBALL");
    feat_epic_spell_mage_armour = feat_array->from_constant("FEAT_EPIC_SPELL_MAGE_ARMOUR");
    feat_epic_spell_ruin = feat_array->from_constant("FEAT_EPIC_SPELL_RUIN");
    feat_weapon_of_choice_sickle = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_SICKLE");
    feat_weapon_of_choice_kama = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_KAMA");
    feat_weapon_of_choice_kukri = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_KUKRI");
    feat_ki_damage = feat_array->from_constant("FEAT_KI_DAMAGE");
    feat_increase_multiplier = feat_array->from_constant("FEAT_INCREASE_MULTIPLIER");
    feat_superior_weapon_focus = feat_array->from_constant("FEAT_SUPERIOR_WEAPON_FOCUS");
    feat_ki_critical = feat_array->from_constant("FEAT_KI_CRITICAL");
    feat_bone_skin_2 = feat_array->from_constant("FEAT_BONE_SKIN_2");
    feat_bone_skin_4 = feat_array->from_constant("FEAT_BONE_SKIN_4");
    feat_bone_skin_6 = feat_array->from_constant("FEAT_BONE_SKIN_6");
    feat_animate_dead = feat_array->from_constant("FEAT_ANIMATE_DEAD");
    feat_summon_undead = feat_array->from_constant("FEAT_SUMMON_UNDEAD");
    feat_deathless_vigor = feat_array->from_constant("FEAT_DEATHLESS_VIGOR");
    feat_undead_graft_1 = feat_array->from_constant("FEAT_UNDEAD_GRAFT_1");
    feat_undead_graft_2 = feat_array->from_constant("FEAT_UNDEAD_GRAFT_2");
    feat_tough_as_bone = feat_array->from_constant("FEAT_TOUGH_AS_BONE");
    feat_summon_greater_undead = feat_array->from_constant("FEAT_SUMMON_GREATER_UNDEAD");
    feat_deathless_mastery = feat_array->from_constant("FEAT_DEATHLESS_MASTERY");
    feat_deathless_master_touch = feat_array->from_constant("FEAT_DEATHLESS_MASTER_TOUCH");
    feat_greater_wildshape_1 = feat_array->from_constant("FEAT_GREATER_WILDSHAPE_1");
    feat_shou_disciple_martial_flurry_any = feat_array->from_constant("FEAT_SHOU_DISCIPLE_MARTIAL_FLURRY_ANY");
    feat_greater_wildshape_2 = feat_array->from_constant("FEAT_GREATER_WILDSHAPE_2");
    feat_greater_wildshape_3 = feat_array->from_constant("FEAT_GREATER_WILDSHAPE_3");
    feat_humanoid_shape = feat_array->from_constant("FEAT_HUMANOID_SHAPE");
    feat_greater_wildshape_4 = feat_array->from_constant("FEAT_GREATER_WILDSHAPE_4");
    feat_sacred_defense_1 = feat_array->from_constant("FEAT_SACRED_DEFENSE_1");
    feat_sacred_defense_2 = feat_array->from_constant("FEAT_SACRED_DEFENSE_2");
    feat_sacred_defense_3 = feat_array->from_constant("FEAT_SACRED_DEFENSE_3");
    feat_sacred_defense_4 = feat_array->from_constant("FEAT_SACRED_DEFENSE_4");
    feat_sacred_defense_5 = feat_array->from_constant("FEAT_SACRED_DEFENSE_5");
    feat_divine_wrath = feat_array->from_constant("FEAT_DIVINE_WRATH");
    feat_extra_smiting = feat_array->from_constant("FEAT_EXTRA_SMITING");
    feat_skill_focus_craft_armor = feat_array->from_constant("FEAT_SKILL_FOCUS_CRAFT_ARMOR");
    feat_skill_focus_craft_weapon = feat_array->from_constant("FEAT_SKILL_FOCUS_CRAFT_WEAPON");
    feat_epic_skill_focus_craft_armor = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_CRAFT_ARMOR");
    feat_epic_skill_focus_craft_weapon = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_CRAFT_WEAPON");
    feat_skill_focus_bluff = feat_array->from_constant("FEAT_SKILL_FOCUS_BLUFF");
    feat_skill_focus_intimidate = feat_array->from_constant("FEAT_SKILL_FOCUS_INTIMIDATE");
    feat_epic_skill_focus_bluff = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_BLUFF");
    feat_epic_skill_focus_intimidate = feat_array->from_constant("FEAT_EPIC_SKILL_FOCUS_INTIMIDATE");

    feat_weapon_of_choice_club = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_CLUB");
    feat_weapon_of_choice_dagger = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_DAGGER");
    feat_weapon_of_choice_lightmace = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_LIGHTMACE");
    feat_weapon_of_choice_morningstar = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_MORNINGSTAR");
    feat_weapon_of_choice_quarterstaff = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_QUARTERSTAFF");
    feat_weapon_of_choice_shortspear = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_SHORTSPEAR");
    feat_weapon_of_choice_shortsword = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_SHORTSWORD");
    feat_weapon_of_choice_rapier = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_RAPIER");
    feat_weapon_of_choice_scimitar = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_SCIMITAR");
    feat_weapon_of_choice_longsword = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_LONGSWORD");
    feat_weapon_of_choice_greatsword = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_GREATSWORD");
    feat_weapon_of_choice_handaxe = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_HANDAXE");
    feat_weapon_of_choice_battleaxe = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_BATTLEAXE");
    feat_weapon_of_choice_greataxe = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_GREATAXE");
    feat_weapon_of_choice_halberd = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_HALBERD");
    feat_weapon_of_choice_lighthammer = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_LIGHTHAMMER");
    feat_weapon_of_choice_lightflail = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_LIGHTFLAIL");
    feat_weapon_of_choice_warhammer = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_WARHAMMER");
    feat_weapon_of_choice_heavyflail = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_HEAVYFLAIL");
    feat_weapon_of_choice_scythe = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_SCYTHE");
    feat_weapon_of_choice_katana = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_KATANA");
    feat_weapon_of_choice_bastardsword = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_BASTARDSWORD");
    feat_weapon_of_choice_diremace = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_DIREMACE");
    feat_weapon_of_choice_doubleaxe = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_DOUBLEAXE");
    feat_weapon_of_choice_twobladedsword = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_TWOBLADEDSWORD");

    feat_brew_potion = feat_array->from_constant("FEAT_BREW_POTION");
    feat_scribe_scroll = feat_array->from_constant("FEAT_SCRIBE_SCROLL");
    feat_craft_wand = feat_array->from_constant("FEAT_CRAFT_WAND");

    feat_dwarven_defender_defensive_stance = feat_array->from_constant("FEAT_DWARVEN_DEFENDER_DEFENSIVE_STANCE");
    feat_damage_reduction_6 = feat_array->from_constant("FEAT_DAMAGE_REDUCTION_6");
    feat_prestige_defensive_awareness_1 = feat_array->from_constant("FEAT_PRESTIGE_DEFENSIVE_AWARENESS_1");
    feat_prestige_defensive_awareness_2 = feat_array->from_constant("FEAT_PRESTIGE_DEFENSIVE_AWARENESS_2");
    feat_prestige_defensive_awareness_3 = feat_array->from_constant("FEAT_PRESTIGE_DEFENSIVE_AWARENESS_3");
    feat_weapon_focus_dwaxe = feat_array->from_constant("FEAT_WEAPON_FOCUS_DWAXE");
    feat_weapon_specialization_dwaxe = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_DWAXE");
    feat_improved_critical_dwaxe = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_DWAXE");
    feat_epic_devastating_critical_dwaxe = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_DWAXE");
    feat_epic_weapon_focus_dwaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_DWAXE");
    feat_epic_weapon_specialization_dwaxe = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_DWAXE");
    feat_epic_overwhelming_critical_dwaxe = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_DWAXE");
    feat_weapon_of_choice_dwaxe = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_DWAXE");
    feat_use_poison = feat_array->from_constant("FEAT_USE_POISON");

    feat_dragon_armor = feat_array->from_constant("FEAT_DRAGON_ARMOR");
    feat_dragon_abilities = feat_array->from_constant("FEAT_DRAGON_ABILITIES");
    feat_dragon_immune_paralysis = feat_array->from_constant("FEAT_DRAGON_IMMUNE_PARALYSIS");
    feat_dragon_immune_fire = feat_array->from_constant("FEAT_DRAGON_IMMUNE_FIRE");
    feat_dragon_dis_breath = feat_array->from_constant("FEAT_DRAGON_DIS_BREATH");
    feat_epic_fighter = feat_array->from_constant("FEAT_EPIC_FIGHTER");
    feat_epic_barbarian = feat_array->from_constant("FEAT_EPIC_BARBARIAN");
    feat_epic_bard = feat_array->from_constant("FEAT_EPIC_BARD");
    feat_epic_cleric = feat_array->from_constant("FEAT_EPIC_CLERIC");
    feat_epic_druid = feat_array->from_constant("FEAT_EPIC_DRUID");
    feat_epic_monk = feat_array->from_constant("FEAT_EPIC_MONK");
    feat_epic_paladin = feat_array->from_constant("FEAT_EPIC_PALADIN");
    feat_epic_ranger = feat_array->from_constant("FEAT_EPIC_RANGER");
    feat_epic_rogue = feat_array->from_constant("FEAT_EPIC_ROGUE");
    feat_epic_sorcerer = feat_array->from_constant("FEAT_EPIC_SORCERER");
    feat_epic_wizard = feat_array->from_constant("FEAT_EPIC_WIZARD");
    feat_epic_arcane_archer = feat_array->from_constant("FEAT_EPIC_ARCANE_ARCHER");
    feat_epic_assassin = feat_array->from_constant("FEAT_EPIC_ASSASSIN");
    feat_epic_blackguard = feat_array->from_constant("FEAT_EPIC_BLACKGUARD");
    feat_epic_shadowdancer = feat_array->from_constant("FEAT_EPIC_SHADOWDANCER");
    feat_epic_harper_scout = feat_array->from_constant("FEAT_EPIC_HARPER_SCOUT");
    feat_epic_divine_champion = feat_array->from_constant("FEAT_EPIC_DIVINE_CHAMPION");
    feat_epic_weapon_master = feat_array->from_constant("FEAT_EPIC_WEAPON_MASTER");
    feat_epic_pale_master = feat_array->from_constant("FEAT_EPIC_PALE_MASTER");
    feat_epic_dwarven_defender = feat_array->from_constant("FEAT_EPIC_DWARVEN_DEFENDER");
    feat_epic_shifter = feat_array->from_constant("FEAT_EPIC_SHIFTER");
    feat_epic_red_dragon_disc = feat_array->from_constant("FEAT_EPIC_RED_DRAGON_DISC");
    feat_epic_thundering_rage = feat_array->from_constant("FEAT_EPIC_THUNDERING_RAGE");
    feat_epic_terrifying_rage = feat_array->from_constant("FEAT_EPIC_TERRIFYING_RAGE");
    feat_epic_spell_epic_warding = feat_array->from_constant("FEAT_EPIC_SPELL_EPIC_WARDING");
    feat_weapon_focus_whip = feat_array->from_constant("FEAT_WEAPON_FOCUS_WHIP");
    feat_weapon_specialization_whip = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_WHIP");
    feat_improved_critical_whip = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_WHIP");
    feat_epic_devastating_critical_whip = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_WHIP");
    feat_epic_weapon_focus_whip = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_WHIP");
    feat_epic_weapon_specialization_whip = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_WHIP");
    feat_epic_overwhelming_critical_whip = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_WHIP");
    feat_weapon_of_choice_whip = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_WHIP");
    feat_epic_character = feat_array->from_constant("FEAT_EPIC_CHARACTER");
    feat_epic_epic_shadowlord = feat_array->from_constant("FEAT_EPIC_EPIC_SHADOWLORD");
    feat_epic_epic_fiend = feat_array->from_constant("FEAT_EPIC_EPIC_FIEND");
    feat_prestige_death_attack_6 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_6");
    feat_prestige_death_attack_7 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_7");
    feat_prestige_death_attack_8 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_8");
    feat_blackguard_sneak_attack_4d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_4D6");
    feat_blackguard_sneak_attack_5d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_5D6");
    feat_blackguard_sneak_attack_6d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_6D6");
    feat_blackguard_sneak_attack_7d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_7D6");
    feat_blackguard_sneak_attack_8d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_8D6");
    feat_blackguard_sneak_attack_9d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_9D6");
    feat_blackguard_sneak_attack_10d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_10D6");
    feat_blackguard_sneak_attack_11d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_11D6");
    feat_blackguard_sneak_attack_12d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_12D6");
    feat_blackguard_sneak_attack_13d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_13D6");
    feat_blackguard_sneak_attack_14d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_14D6");
    feat_blackguard_sneak_attack_15d6 = feat_array->from_constant("FEAT_BLACKGUARD_SNEAK_ATTACK_15D6");
    feat_prestige_death_attack_9 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_9");
    feat_prestige_death_attack_10 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_10");
    feat_prestige_death_attack_11 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_11");
    feat_prestige_death_attack_12 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_12");
    feat_prestige_death_attack_13 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_13");
    feat_prestige_death_attack_14 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_14");
    feat_prestige_death_attack_15 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_15");
    feat_prestige_death_attack_16 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_16");
    feat_prestige_death_attack_17 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_17");
    feat_prestige_death_attack_18 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_18");
    feat_prestige_death_attack_19 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_19");
    feat_prestige_death_attack_20 = feat_array->from_constant("FEAT_PRESTIGE_DEATH_ATTACK_20");
    feat_shou_disciple_dodge_3 = feat_array->from_constant("FEAT_SHOU_DISCIPLE_DODGE_3");
    feat_dragon_hdincrease_d6 = feat_array->from_constant("FEAT_DRAGON_HDINCREASE_D6");
    feat_dragon_hdincrease_d8 = feat_array->from_constant("FEAT_DRAGON_HDINCREASE_D8");
    feat_dragon_hdincrease_d10 = feat_array->from_constant("FEAT_DRAGON_HDINCREASE_D10");
    feat_prestige_enchant_arrow_6 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_6");
    feat_prestige_enchant_arrow_7 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_7");
    feat_prestige_enchant_arrow_8 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_8");
    feat_prestige_enchant_arrow_9 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_9");
    feat_prestige_enchant_arrow_10 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_10");
    feat_prestige_enchant_arrow_11 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_11");
    feat_prestige_enchant_arrow_12 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_12");
    feat_prestige_enchant_arrow_13 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_13");
    feat_prestige_enchant_arrow_14 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_14");
    feat_prestige_enchant_arrow_15 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_15");
    feat_prestige_enchant_arrow_16 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_16");
    feat_prestige_enchant_arrow_17 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_17");
    feat_prestige_enchant_arrow_18 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_18");
    feat_prestige_enchant_arrow_19 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_19");
    feat_prestige_enchant_arrow_20 = feat_array->from_constant("FEAT_PRESTIGE_ENCHANT_ARROW_20");
    feat_epic_outsider_shape = feat_array->from_constant("FEAT_EPIC_OUTSIDER_SHAPE");
    feat_epic_construct_shape = feat_array->from_constant("FEAT_EPIC_CONSTRUCT_SHAPE");
    feat_epic_shifter_infinite_wildshape_1 = feat_array->from_constant("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_1");
    feat_epic_shifter_infinite_wildshape_2 = feat_array->from_constant("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_2");
    feat_epic_shifter_infinite_wildshape_3 = feat_array->from_constant("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_3");
    feat_epic_shifter_infinite_wildshape_4 = feat_array->from_constant("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_4");
    feat_epic_shifter_infinite_humanoid_shape = feat_array->from_constant("FEAT_EPIC_SHIFTER_INFINITE_HUMANOID_SHAPE");
    feat_epic_barbarian_damage_reduction = feat_array->from_constant("FEAT_EPIC_BARBARIAN_DAMAGE_REDUCTION");
    feat_epic_druid_infinite_wildshape = feat_array->from_constant("FEAT_EPIC_DRUID_INFINITE_WILDSHAPE");
    feat_epic_druid_infinite_elemental_shape = feat_array->from_constant("FEAT_EPIC_DRUID_INFINITE_ELEMENTAL_SHAPE");
    feat_prestige_poison_save_epic = feat_array->from_constant("FEAT_PRESTIGE_POISON_SAVE_EPIC");
    feat_epic_superior_weapon_focus = feat_array->from_constant("FEAT_EPIC_SUPERIOR_WEAPON_FOCUS");

    feat_weapon_focus_trident = feat_array->from_constant("FEAT_WEAPON_FOCUS_TRIDENT");
    feat_weapon_specialization_trident = feat_array->from_constant("FEAT_WEAPON_SPECIALIZATION_TRIDENT");
    feat_improved_critical_trident = feat_array->from_constant("FEAT_IMPROVED_CRITICAL_TRIDENT");
    feat_epic_devastating_critical_trident = feat_array->from_constant("FEAT_EPIC_DEVASTATING_CRITICAL_TRIDENT");
    feat_epic_weapon_focus_trident = feat_array->from_constant("FEAT_EPIC_WEAPON_FOCUS_TRIDENT");
    feat_epic_weapon_specialization_trident = feat_array->from_constant("FEAT_EPIC_WEAPON_SPECIALIZATION_TRIDENT");
    feat_epic_overwhelming_critical_trident = feat_array->from_constant("FEAT_EPIC_OVERWHELMING_CRITICAL_TRIDENT");
    feat_weapon_of_choice_trident = feat_array->from_constant("FEAT_WEAPON_OF_CHOICE_TRIDENT");
    feat_pdk_rally = feat_array->from_constant("FEAT_PDK_RALLY");
    feat_pdk_shield = feat_array->from_constant("FEAT_PDK_SHIELD");
    feat_pdk_fear = feat_array->from_constant("FEAT_PDK_FEAR");
    feat_pdk_wrath = feat_array->from_constant("FEAT_PDK_WRATH");
    feat_pdk_stand = feat_array->from_constant("FEAT_PDK_STAND");
    feat_pdk_inspire_1 = feat_array->from_constant("FEAT_PDK_INSPIRE_1");
    feat_pdk_inspire_2 = feat_array->from_constant("FEAT_PDK_INSPIRE_2");
    feat_mounted_combat = feat_array->from_constant("FEAT_MOUNTED_COMBAT");
    feat_mounted_archery = feat_array->from_constant("FEAT_MOUNTED_ARCHERY");
    feat_horse_menu = feat_array->from_constant("FEAT_HORSE_MENU");
    feat_horse_mount = feat_array->from_constant("FEAT_HORSE_MOUNT");
    feat_horse_dismount = feat_array->from_constant("FEAT_HORSE_DISMOUNT");
    feat_horse_party_mount = feat_array->from_constant("FEAT_HORSE_PARTY_MOUNT");
    feat_horse_party_dismount = feat_array->from_constant("FEAT_HORSE_PARTY_DISMOUNT");
    feat_horse_assign_mount = feat_array->from_constant("FEAT_HORSE_ASSIGN_MOUNT");
    feat_paladin_summon_mount = feat_array->from_constant("FEAT_PALADIN_SUMMON_MOUNT");
    feat_player_tool_01 = feat_array->from_constant("FEAT_PLAYER_TOOL_01");
    feat_player_tool_02 = feat_array->from_constant("FEAT_PLAYER_TOOL_02");
    feat_player_tool_03 = feat_array->from_constant("FEAT_PLAYER_TOOL_03");
    feat_player_tool_04 = feat_array->from_constant("FEAT_PLAYER_TOOL_04");
    feat_player_tool_05 = feat_array->from_constant("FEAT_PLAYER_TOOL_05");
    feat_player_tool_06 = feat_array->from_constant("FEAT_PLAYER_TOOL_06");
    feat_player_tool_07 = feat_array->from_constant("FEAT_PLAYER_TOOL_07");
    feat_player_tool_08 = feat_array->from_constant("FEAT_PLAYER_TOOL_08");
    feat_player_tool_09 = feat_array->from_constant("FEAT_PLAYER_TOOL_09");
    feat_player_tool_10 = feat_array->from_constant("FEAT_PLAYER_TOOL_10");

    return true;
}

bool Profile::load_components() const
{
    LOG_F(INFO, "[nwn1] loading components...");
    nw::kernel::world().add<nw::AbilityArray>();
    nw::kernel::world().add<nw::BaseItemArray>();
    nw::kernel::world().add<nw::ClassArray>();
    nw::kernel::world().add<nw::FeatArray>();
    nw::kernel::world().add<nw::MasterFeatRegistry>();
    nw::kernel::world().add<nw::RaceArray>();
    nw::kernel::world().add<nw::SkillArray>();
    nw::kernel::world().add<nw::SpellArray>();
    return true;
}

bool Profile::load_rules() const
{
    LOG_F(INFO, "[nwn1] loading rules...");

    // == Set global rule functions ===========================================
    nw::kernel::rules().set_selector(selector);
    nw::kernel::rules().set_qualifier(match);

    // == Load Compontents ====================================================
    load_components();

    // == Load 2das ===========================================================

    nw::TwoDA baseitems{nw::kernel::resman().demand({"baseitems"sv, nw::ResourceType::twoda})};
    nw::TwoDA classes{nw::kernel::resman().demand({"classes"sv, nw::ResourceType::twoda})};
    nw::TwoDA feat{nw::kernel::resman().demand({"feat"sv, nw::ResourceType::twoda})};
    nw::TwoDA racialtypes{nw::kernel::resman().demand({"racialtypes"sv, nw::ResourceType::twoda})};
    nw::TwoDA skills{nw::kernel::resman().demand({"skills"sv, nw::ResourceType::twoda})};
    nw::TwoDA spells{nw::kernel::resman().demand({"spells"sv, nw::ResourceType::twoda})};
    std::string temp_string;
    int temp_int = 0;

    // Abilities - Temporary, no 2da..
    auto* ability_array = nw::kernel::world().get_mut<nw::AbilityArray>();
    ability_array->entries.reserve(6);
    auto cnst = nw::kernel::strings().intern("ABILITY_STRENGTH");
    ability_array->entries.push_back({135, cnst});
    cnst = nw::kernel::strings().intern("ABILITY_DEXTERITY");
    ability_array->entries.push_back({133, cnst});
    cnst = nw::kernel::strings().intern("ABILITY_CONSTITUTION");
    ability_array->entries.push_back({132, cnst});
    cnst = nw::kernel::strings().intern("ABILITY_INTELLIGENCE");
    ability_array->entries.push_back({134, cnst});
    cnst = nw::kernel::strings().intern("ABILITY_WISDOM");
    ability_array->entries.push_back({136, cnst});
    cnst = nw::kernel::strings().intern("ABILITY_CHARISMA");
    ability_array->entries.push_back({131, cnst});
    for (uint32_t i = 0; i < 6; ++i) {
        ability_array->constant_to_index.emplace(ability_array->entries[i].constant, nw::make_ability(i));
    }

    // BaseItems
    auto* baseitem_array = nw::kernel::world().get_mut<nw::BaseItemArray>();
    auto* mfr = nw::kernel::world().get_mut<nw::MasterFeatRegistry>();
    if (mfr && baseitem_array && baseitems.is_valid()) {
        for (size_t i = 0; i < baseitems.rows(); ++i) {
            auto& info = baseitem_array->entries.emplace_back(baseitems.row(i));
            if (info.valid()) {
                if (baseitems.get_to(i, "WeaponFocusFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_weapon_focus, nw::make_feat(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponFocusFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_weapon_focus_epic, nw::make_feat(temp_int));
                }
                if (baseitems.get_to(i, "WeaponSpecializationFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_weapon_spec, nw::make_feat(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponSpecializationFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_weapon_spec_epic, nw::make_feat(temp_int));
                }
                if (baseitems.get_to(i, "WeaponImprovedCriticalFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_improved_crit, nw::make_feat(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponOverwhelmingCriticalFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_overwhelming_crit, nw::make_feat(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponDevastatingCriticalFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_devastating_crit, nw::make_feat(temp_int));
                }
                if (baseitems.get_to(i, "WeaponOfChoiceFeat", temp_int)) {
                    mfr->add(nw::make_baseitem(static_cast<int32_t>(i)),
                        mfeat_weapon_of_choice, nw::make_feat(temp_int));
                }
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'baseitems.2da'");
    }

    // Classes
    auto* class_array = nw::kernel::world().get_mut<nw::ClassArray>();
    if (class_array && classes.is_valid()) {
        class_array->entries.reserve(classes.rows());
        for (size_t i = 0; i < classes.rows(); ++i) {
            const auto& info = class_array->entries.emplace_back(classes.row(i));
            if (info.constant) {
                class_array->constant_to_index.emplace(info.constant, nw::make_class(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid class ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'classes.2da'");
    }

    // Feats
    auto* feat_array = nw::kernel::world().get_mut<nw::FeatArray>();
    if (feat_array && feat.is_valid()) {
        feat_array->entries.reserve(feat.rows());
        for (size_t i = 0; i < feat.rows(); ++i) {
            const auto& info = feat_array->entries.emplace_back(feat.row(i));
            if (info.constant) {
                feat_array->constant_to_index.emplace(info.constant, nw::make_feat(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid feat ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'feat.2da'");
    }

    // Races
    auto* race_array = nw::kernel::world().get_mut<nw::RaceArray>();
    if (race_array && racialtypes.is_valid()) {
        race_array->entries.reserve(racialtypes.rows());
        for (size_t i = 0; i < racialtypes.rows(); ++i) {
            const auto& info = race_array->entries.emplace_back(racialtypes.row(i));
            if (info.constant) {
                race_array->constant_to_index.emplace(info.constant, nw::make_race(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid race ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'racialtypes.2da'");
    }

    // Skills
    auto* skill_array = nw::kernel::world().get_mut<nw::SkillArray>();
    if (skill_array && skills.is_valid()) {
        skill_array->entries.reserve(skills.rows());
        for (size_t i = 0; i < skills.rows(); ++i) {
            const auto& info = skill_array->entries.emplace_back(skills.row(i));
            if (info.constant) {
                skill_array->constant_to_index.emplace(info.constant, nw::make_skill(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid skill ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'skills.2da'");
    }

    // Spells
    auto* spell_array = nw::kernel::world().get_mut<nw::SpellArray>();
    if (spell_array && spells.is_valid()) {
        spell_array->entries.reserve(spells.rows());
        for (size_t i = 0; i < spells.rows(); ++i) {
            spell_array->entries.emplace_back(spells.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'spells.2da'");
    }

    // == Load Constants ======================================================
    load_constants();

    // == Postprocess 2das ====================================================

    // BaseItems
    for (size_t i = 0; i < baseitem_array->entries.size(); ++i) {
        nw::BaseItemInfo& info = baseitem_array->entries[i];
        if (baseitems.get_to(i, "ReqFeat0", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::make_feat(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat1", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::make_feat(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat2", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::make_feat(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat3", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::make_feat(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat4", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::make_feat(temp_int)));
        }
    }

    // Classes

    // Feats

    for (size_t i = 0; i < feat_array->entries.size(); ++i) {
        nw::FeatInfo& info = feat_array->entries[i];
        if (!info.valid()) {
            continue;
        }

        if (feat.get_to(i, "MaxLevel", temp_int)) {
            info.requirements.add(qual::level(0, temp_int));
        }

        if (feat.get_to(i, "MINSTR", temp_int)) {
            info.requirements.add(qual::ability(ability_strength, temp_int));
        }

        if (feat.get_to(i, "MINDEX", temp_int)) {
            info.requirements.add(qual::ability(ability_dexterity, temp_int));
        }

        if (feat.get_to(i, "MINCON", temp_int)) {
            info.requirements.add(qual::ability(ability_constitution, temp_int));
        }

        if (feat.get_to(i, "MININT", temp_int)) {
            info.requirements.add(qual::ability(ability_intelligence, temp_int));
        }

        if (feat.get_to(i, "MINWIS", temp_int)) {
            info.requirements.add(qual::ability(ability_wisdom, temp_int));
        }

        if (feat.get_to(i, "MINCHA", temp_int)) {
            info.requirements.add(qual::ability(ability_charisma, temp_int));
        }

        if (feat.get_to(i, "PREREQFEAT1", temp_int)) {
            info.requirements.add(qual::feat(nw::make_feat(temp_int)));
        }

        if (feat.get_to(i, "PREREQFEAT2", temp_int)) {
            info.requirements.add(qual::feat(nw::make_feat(temp_int)));
        }
    }

    // Races

    // Skill
    for (size_t i = 0; i < skills.rows(); ++i) {
        auto& info = skill_array->entries[i];
        if (skills.get_to(i, "KeyAbility", temp_string)) {
            if (nw::string::icmp("str", temp_string)) {
                info.ability = ability_strength;
            } else if (nw::string::icmp("dex", temp_string)) {
                info.ability = ability_dexterity;
            } else if (nw::string::icmp("con", temp_string)) {
                info.ability = ability_constitution;
            } else if (nw::string::icmp("int", temp_string)) {
                info.ability = ability_intelligence;
            } else if (nw::string::icmp("wis", temp_string)) {
                info.ability = ability_wisdom;
            } else if (nw::string::icmp("cha", temp_string)) {
                info.ability = ability_charisma;
            }
        }
    }

    // == Load Modifiers ======================================================
    load_modifiers();

    // == Load Master Feats ===================================================
    load_master_feats();

    return true;
}

bool Profile::load_master_feats() const
{
    LOG_F(INFO, "[nwn1] Loading Master Feats");

    auto mfr = nw::kernel::world().get_mut<nw::MasterFeatRegistry>();
    mfr->set_bonus(mfeat_skill_focus, 3);
    mfr->set_bonus(mfeat_skill_focus_epic, 10);

#define ADD_SKILL(name)                                                 \
    mfr->add(skill_##name, mfeat_skill_focus, feat_skill_focus_##name); \
    mfr->add(skill_##name, mfeat_skill_focus_epic, feat_epic_skill_focus_##name)

    ADD_SKILL(animal_empathy);
    ADD_SKILL(concentration);
    ADD_SKILL(disable_trap);
    ADD_SKILL(discipline);
    ADD_SKILL(heal);
    ADD_SKILL(hide);
    ADD_SKILL(listen);
    ADD_SKILL(lore);
    ADD_SKILL(move_silently);
    ADD_SKILL(open_lock);
    ADD_SKILL(parry);
    ADD_SKILL(perform);
    ADD_SKILL(persuade);
    ADD_SKILL(pick_pocket);
    ADD_SKILL(search);
    ADD_SKILL(set_trap);
    ADD_SKILL(spellcraft);
    ADD_SKILL(spot);
    ADD_SKILL(taunt);
    ADD_SKILL(use_magic_device);
    // ADD_SKILL(appraise);
    ADD_SKILL(tumble);
    ADD_SKILL(craft_trap);
    ADD_SKILL(bluff);
    ADD_SKILL(intimidate);
    ADD_SKILL(craft_armor);
    ADD_SKILL(craft_weapon);
    // ADD_SKILL(ride);

#undef ADD_SKILL

    return true;
}

} // namespace nwn1
