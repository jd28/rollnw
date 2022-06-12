#include "Profile.hpp"

#include "rules.hpp"

#include "../../formats/TwoDA.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/components/IndexRegistry.hpp"
#include "../../objects/Creature.hpp"
#include "../../rules/Ability.hpp"
#include "../../rules/BaseItem.hpp"
#include "../../rules/Class.hpp"
#include "../../rules/Feat.hpp"
#include "../../rules/Race.hpp"
#include "../../rules/Skill.hpp"
#include "../../rules/Spell.hpp"
#include "../../rules/system.hpp"
#include "modifiers.hpp"

namespace nwn1 {

bool Profile::load_constants() const
{
    auto* cr = nw::kernel::world().get_mut<nw::IndexRegistry>();
    if (!cr) { return false; }

    ability_strength = cr->add("ABILITY_STRENGTH", 0);
    ability_dexterity = cr->add("ABILITY_DEXTERITY", 1);
    ability_constitution = cr->add("ABILITY_CONSTITUTION", 2);
    ability_intelligence = cr->add("ABILITY_INTELLIGENCE", 3);
    ability_wisdom = cr->add("ABILITY_WISDOM", 4);
    ability_charisma = cr->add("ABILITY_CHARISMA", 5);

    class_type_barbarian = cr->add("CLASS_TYPE_BARBARIAN", 0);
    class_type_bard = cr->add("CLASS_TYPE_BARD", 1);
    class_type_cleric = cr->add("CLASS_TYPE_CLERIC", 2);
    class_type_druid = cr->add("CLASS_TYPE_DRUID", 3);
    class_type_fighter = cr->add("CLASS_TYPE_FIGHTER", 4);
    class_type_monk = cr->add("CLASS_TYPE_MONK", 5);
    class_type_paladin = cr->add("CLASS_TYPE_PALADIN", 6);
    class_type_ranger = cr->add("CLASS_TYPE_RANGER", 7);
    class_type_rogue = cr->add("CLASS_TYPE_ROGUE", 8);
    class_type_sorcerer = cr->add("CLASS_TYPE_SORCERER", 9);
    class_type_wizard = cr->add("CLASS_TYPE_WIZARD", 10);
    class_type_aberration = cr->add("CLASS_TYPE_ABERRATION", 11);
    class_type_animal = cr->add("CLASS_TYPE_ANIMAL", 12);
    class_type_construct = cr->add("CLASS_TYPE_CONSTRUCT", 13);
    class_type_humanoid = cr->add("CLASS_TYPE_HUMANOID", 14);
    class_type_monstrous = cr->add("CLASS_TYPE_MONSTROUS", 15);
    class_type_elemental = cr->add("CLASS_TYPE_ELEMENTAL", 16);
    class_type_fey = cr->add("CLASS_TYPE_FEY", 17);
    class_type_dragon = cr->add("CLASS_TYPE_DRAGON", 18);
    class_type_undead = cr->add("CLASS_TYPE_UNDEAD", 19);
    class_type_commoner = cr->add("CLASS_TYPE_COMMONER", 20);
    class_type_beast = cr->add("CLASS_TYPE_BEAST", 21);
    class_type_giant = cr->add("CLASS_TYPE_GIANT", 22);
    class_type_magical_beast = cr->add("CLASS_TYPE_MAGICAL_BEAST", 23);
    class_type_outsider = cr->add("CLASS_TYPE_OUTSIDER", 24);
    class_type_shapechanger = cr->add("CLASS_TYPE_SHAPECHANGER", 25);
    class_type_vermin = cr->add("CLASS_TYPE_VERMIN", 26);
    class_type_shadowdancer = cr->add("CLASS_TYPE_SHADOWDANCER", 27);
    class_type_harper = cr->add("CLASS_TYPE_HARPER", 28);
    class_type_arcane_archer = cr->add("CLASS_TYPE_ARCANE_ARCHER", 29);
    class_type_assassin = cr->add("CLASS_TYPE_ASSASSIN", 30);
    class_type_blackguard = cr->add("CLASS_TYPE_BLACKGUARD", 31);
    class_type_divinechampion = cr->add("CLASS_TYPE_DIVINECHAMPION", 32);
    class_type_divine_champion = cr->add("CLASS_TYPE_DIVINE_CHAMPION", 32);
    class_type_weapon_master = cr->add("CLASS_TYPE_WEAPON_MASTER", 33);
    class_type_palemaster = cr->add("CLASS_TYPE_PALEMASTER", 34);
    class_type_pale_master = cr->add("CLASS_TYPE_PALE_MASTER", 34);
    class_type_shifter = cr->add("CLASS_TYPE_SHIFTER", 35);
    class_type_dwarvendefender = cr->add("CLASS_TYPE_DWARVENDEFENDER", 36);
    class_type_dwarven_defender = cr->add("CLASS_TYPE_DWARVEN_DEFENDER", 36);
    class_type_dragondisciple = cr->add("CLASS_TYPE_DRAGONDISCIPLE", 37);
    class_type_dragon_disciple = cr->add("CLASS_TYPE_DRAGON_DISCIPLE", 37);
    class_type_ooze = cr->add("CLASS_TYPE_OOZE", 38);
    class_type_eye_of_gruumsh = cr->add("CLASS_TYPE_EYE_OF_GRUUMSH", 39);
    class_type_shou_disciple = cr->add("CLASS_TYPE_SHOU_DISCIPLE", 40);
    class_type_purple_dragon_knight = cr->add("CLASS_TYPE_PURPLE_DRAGON_KNIGHT", 41);

    racial_type_dwarf = cr->add("RACIAL_TYPE_DWARF", 0);
    racial_type_elf = cr->add("RACIAL_TYPE_ELF", 1);
    racial_type_gnome = cr->add("RACIAL_TYPE_GNOME", 2);
    racial_type_halfling = cr->add("RACIAL_TYPE_HALFLING", 3);
    racial_type_halfelf = cr->add("RACIAL_TYPE_HALFELF", 4);
    racial_type_halforc = cr->add("RACIAL_TYPE_HALFORC", 5);
    racial_type_human = cr->add("RACIAL_TYPE_HUMAN", 6);
    racial_type_aberration = cr->add("RACIAL_TYPE_ABERRATION", 7);
    racial_type_animal = cr->add("RACIAL_TYPE_ANIMAL", 8);
    racial_type_beast = cr->add("RACIAL_TYPE_BEAST", 9);
    racial_type_construct = cr->add("RACIAL_TYPE_CONSTRUCT", 10);
    racial_type_dragon = cr->add("RACIAL_TYPE_DRAGON", 11);
    racial_type_humanoid_goblinoid = cr->add("RACIAL_TYPE_HUMANOID_GOBLINOID", 12);
    racial_type_humanoid_monstrous = cr->add("RACIAL_TYPE_HUMANOID_MONSTROUS", 13);
    racial_type_humanoid_orc = cr->add("RACIAL_TYPE_HUMANOID_ORC", 14);
    racial_type_humanoid_reptilian = cr->add("RACIAL_TYPE_HUMANOID_REPTILIAN", 15);
    racial_type_elemental = cr->add("RACIAL_TYPE_ELEMENTAL", 16);
    racial_type_fey = cr->add("RACIAL_TYPE_FEY", 17);
    racial_type_giant = cr->add("RACIAL_TYPE_GIANT", 18);
    racial_type_magical_beast = cr->add("RACIAL_TYPE_MAGICAL_BEAST", 19);
    racial_type_outsider = cr->add("RACIAL_TYPE_OUTSIDER", 20);
    racial_type_shapechanger = cr->add("RACIAL_TYPE_SHAPECHANGER", 23);
    racial_type_undead = cr->add("RACIAL_TYPE_UNDEAD", 24);
    racial_type_vermin = cr->add("RACIAL_TYPE_VERMIN", 25);
    racial_type_all = cr->add("RACIAL_TYPE_ALL", 28);
    racial_type_invalid = cr->add("RACIAL_TYPE_INVALID", 28);
    racial_type_ooze = cr->add("RACIAL_TYPE_OOZE", 29);

    skill_animal_empathy = cr->add("SKILL_ANIMAL_EMPATHY", 0);
    skill_concentration = cr->add("SKILL_CONCENTRATION", 1);
    skill_disable_trap = cr->add("SKILL_DISABLE_TRAP", 2);
    skill_discipline = cr->add("SKILL_DISCIPLINE", 3);
    skill_heal = cr->add("SKILL_HEAL", 4);
    skill_hide = cr->add("SKILL_HIDE", 5);
    skill_listen = cr->add("SKILL_LISTEN", 6);
    skill_lore = cr->add("SKILL_LORE", 7);
    skill_move_silently = cr->add("SKILL_MOVE_SILENTLY", 8);
    skill_open_lock = cr->add("SKILL_OPEN_LOCK", 9);
    skill_parry = cr->add("SKILL_PARRY", 10);
    skill_perform = cr->add("SKILL_PERFORM", 11);
    skill_persuade = cr->add("SKILL_PERSUADE", 12);
    skill_pick_pocket = cr->add("SKILL_PICK_POCKET", 13);
    skill_search = cr->add("SKILL_SEARCH", 14);
    skill_set_trap = cr->add("SKILL_SET_TRAP", 15);
    skill_spellcraft = cr->add("SKILL_SPELLCRAFT", 16);
    skill_spot = cr->add("SKILL_SPOT", 17);
    skill_taunt = cr->add("SKILL_TAUNT", 18);
    skill_use_magic_device = cr->add("SKILL_USE_MAGIC_DEVICE", 19);
    skill_appraise = cr->add("SKILL_APPRAISE", 20);
    skill_tumble = cr->add("SKILL_TUMBLE", 21);
    skill_craft_trap = cr->add("SKILL_CRAFT_TRAP", 22);
    skill_bluff = cr->add("SKILL_BLUFF", 23);
    skill_intimidate = cr->add("SKILL_INTIMIDATE", 24);
    skill_craft_armor = cr->add("SKILL_CRAFT_ARMOR", 25);
    skill_craft_weapon = cr->add("SKILL_CRAFT_WEAPON", 26);
    skill_ride = cr->add("SKILL_RIDE", 27);

    feat_alertness = cr->add("FEAT_ALERTNESS", 0);
    feat_ambidexterity = cr->add("FEAT_AMBIDEXTERITY", 1);
    feat_armor_proficiency_heavy = cr->add("FEAT_ARMOR_PROFICIENCY_HEAVY", 2);
    feat_armor_proficiency_light = cr->add("FEAT_ARMOR_PROFICIENCY_LIGHT", 3);
    feat_armor_proficiency_medium = cr->add("FEAT_ARMOR_PROFICIENCY_MEDIUM", 4);
    feat_called_shot = cr->add("FEAT_CALLED_SHOT", 5);
    feat_cleave = cr->add("FEAT_CLEAVE", 6);
    feat_combat_casting = cr->add("FEAT_COMBAT_CASTING", 7);
    feat_deflect_arrows = cr->add("FEAT_DEFLECT_ARROWS", 8);
    feat_disarm = cr->add("FEAT_DISARM", 9);
    feat_dodge = cr->add("FEAT_DODGE", 10);
    feat_empower_spell = cr->add("FEAT_EMPOWER_SPELL", 11);
    feat_extend_spell = cr->add("FEAT_EXTEND_SPELL", 12);
    feat_extra_turning = cr->add("FEAT_EXTRA_TURNING", 13);
    feat_great_fortitude = cr->add("FEAT_GREAT_FORTITUDE", 14);
    feat_improved_critical_club = cr->add("FEAT_IMPROVED_CRITICAL_CLUB", 15);
    feat_improved_disarm = cr->add("FEAT_IMPROVED_DISARM", 16);
    feat_improved_knockdown = cr->add("FEAT_IMPROVED_KNOCKDOWN", 17);
    feat_improved_parry = cr->add("FEAT_IMPROVED_PARRY", 18);
    feat_improved_power_attack = cr->add("FEAT_IMPROVED_POWER_ATTACK", 19);
    feat_improved_two_weapon_fighting = cr->add("FEAT_IMPROVED_TWO_WEAPON_FIGHTING", 20);
    feat_improved_unarmed_strike = cr->add("FEAT_IMPROVED_UNARMED_STRIKE", 21);
    feat_iron_will = cr->add("FEAT_IRON_WILL", 22);
    feat_knockdown = cr->add("FEAT_KNOCKDOWN", 23);
    feat_lightning_reflexes = cr->add("FEAT_LIGHTNING_REFLEXES", 24);
    feat_maximize_spell = cr->add("FEAT_MAXIMIZE_SPELL", 25);
    feat_mobility = cr->add("FEAT_MOBILITY", 26);
    feat_point_blank_shot = cr->add("FEAT_POINT_BLANK_SHOT", 27);
    feat_power_attack = cr->add("FEAT_POWER_ATTACK", 28);
    feat_quicken_spell = cr->add("FEAT_QUICKEN_SPELL", 29);
    feat_rapid_shot = cr->add("FEAT_RAPID_SHOT", 30);
    feat_sap = cr->add("FEAT_SAP", 31);
    feat_shield_proficiency = cr->add("FEAT_SHIELD_PROFICIENCY", 32);
    feat_silence_spell = cr->add("FEAT_SILENCE_SPELL", 33);
    feat_skill_focus_animal_empathy = cr->add("FEAT_SKILL_FOCUS_ANIMAL_EMPATHY", 34);
    feat_spell_focus_abjuration = cr->add("FEAT_SPELL_FOCUS_ABJURATION", 35);
    feat_spell_penetration = cr->add("FEAT_SPELL_PENETRATION", 36);
    feat_still_spell = cr->add("FEAT_STILL_SPELL", 37);
    feat_stunning_fist = cr->add("FEAT_STUNNING_FIST", 39);
    feat_toughness = cr->add("FEAT_TOUGHNESS", 40);
    feat_two_weapon_fighting = cr->add("FEAT_TWO_WEAPON_FIGHTING", 41);
    feat_weapon_finesse = cr->add("FEAT_WEAPON_FINESSE", 42);
    feat_weapon_focus_club = cr->add("FEAT_WEAPON_FOCUS_CLUB", 43);
    feat_weapon_proficiency_exotic = cr->add("FEAT_WEAPON_PROFICIENCY_EXOTIC", 44);
    feat_weapon_proficiency_martial = cr->add("FEAT_WEAPON_PROFICIENCY_MARTIAL", 45);
    feat_weapon_proficiency_simple = cr->add("FEAT_WEAPON_PROFICIENCY_SIMPLE", 46);
    feat_weapon_specialization_club = cr->add("FEAT_WEAPON_SPECIALIZATION_CLUB", 47);
    feat_weapon_proficiency_druid = cr->add("FEAT_WEAPON_PROFICIENCY_DRUID", 48);
    feat_weapon_proficiency_monk = cr->add("FEAT_WEAPON_PROFICIENCY_MONK", 49);
    feat_weapon_proficiency_rogue = cr->add("FEAT_WEAPON_PROFICIENCY_ROGUE", 50);
    feat_weapon_proficiency_wizard = cr->add("FEAT_WEAPON_PROFICIENCY_WIZARD", 51);
    feat_improved_critical_dagger = cr->add("FEAT_IMPROVED_CRITICAL_DAGGER", 52);
    feat_improved_critical_dart = cr->add("FEAT_IMPROVED_CRITICAL_DART", 53);
    feat_improved_critical_heavy_crossbow = cr->add("FEAT_IMPROVED_CRITICAL_HEAVY_CROSSBOW", 54);
    feat_improved_critical_light_crossbow = cr->add("FEAT_IMPROVED_CRITICAL_LIGHT_CROSSBOW", 55);
    feat_improved_critical_light_mace = cr->add("FEAT_IMPROVED_CRITICAL_LIGHT_MACE", 56);
    feat_improved_critical_morning_star = cr->add("FEAT_IMPROVED_CRITICAL_MORNING_STAR", 57);
    feat_improved_critical_staff = cr->add("FEAT_IMPROVED_CRITICAL_STAFF", 58);
    feat_improved_critical_spear = cr->add("FEAT_IMPROVED_CRITICAL_SPEAR", 59);
    feat_improved_critical_sickle = cr->add("FEAT_IMPROVED_CRITICAL_SICKLE", 60);
    feat_improved_critical_sling = cr->add("FEAT_IMPROVED_CRITICAL_SLING", 61);
    feat_improved_critical_unarmed_strike = cr->add("FEAT_IMPROVED_CRITICAL_UNARMED_STRIKE", 62);
    feat_improved_critical_longbow = cr->add("FEAT_IMPROVED_CRITICAL_LONGBOW", 63);
    feat_improved_critical_shortbow = cr->add("FEAT_IMPROVED_CRITICAL_SHORTBOW", 64);
    feat_improved_critical_short_sword = cr->add("FEAT_IMPROVED_CRITICAL_SHORT_SWORD", 65);
    feat_improved_critical_rapier = cr->add("FEAT_IMPROVED_CRITICAL_RAPIER", 66);
    feat_improved_critical_scimitar = cr->add("FEAT_IMPROVED_CRITICAL_SCIMITAR", 67);
    feat_improved_critical_long_sword = cr->add("FEAT_IMPROVED_CRITICAL_LONG_SWORD", 68);
    feat_improved_critical_great_sword = cr->add("FEAT_IMPROVED_CRITICAL_GREAT_SWORD", 69);
    feat_improved_critical_hand_axe = cr->add("FEAT_IMPROVED_CRITICAL_HAND_AXE", 70);
    feat_improved_critical_throwing_axe = cr->add("FEAT_IMPROVED_CRITICAL_THROWING_AXE", 71);
    feat_improved_critical_battle_axe = cr->add("FEAT_IMPROVED_CRITICAL_BATTLE_AXE", 72);
    feat_improved_critical_great_axe = cr->add("FEAT_IMPROVED_CRITICAL_GREAT_AXE", 73);
    feat_improved_critical_halberd = cr->add("FEAT_IMPROVED_CRITICAL_HALBERD", 74);
    feat_improved_critical_light_hammer = cr->add("FEAT_IMPROVED_CRITICAL_LIGHT_HAMMER", 75);
    feat_improved_critical_light_flail = cr->add("FEAT_IMPROVED_CRITICAL_LIGHT_FLAIL", 76);
    feat_improved_critical_war_hammer = cr->add("FEAT_IMPROVED_CRITICAL_WAR_HAMMER", 77);
    feat_improved_critical_heavy_flail = cr->add("FEAT_IMPROVED_CRITICAL_HEAVY_FLAIL", 78);
    feat_improved_critical_kama = cr->add("FEAT_IMPROVED_CRITICAL_KAMA", 79);
    feat_improved_critical_kukri = cr->add("FEAT_IMPROVED_CRITICAL_KUKRI", 80);
    // feat_improved_critical_nunchaku = cr->add( "FEAT_IMPROVED_CRITICAL_NUNCHAKU" , 81);
    feat_improved_critical_shuriken = cr->add("FEAT_IMPROVED_CRITICAL_SHURIKEN", 82);
    feat_improved_critical_scythe = cr->add("FEAT_IMPROVED_CRITICAL_SCYTHE", 83);
    feat_improved_critical_katana = cr->add("FEAT_IMPROVED_CRITICAL_KATANA", 84);
    feat_improved_critical_bastard_sword = cr->add("FEAT_IMPROVED_CRITICAL_BASTARD_SWORD", 85);
    feat_improved_critical_dire_mace = cr->add("FEAT_IMPROVED_CRITICAL_DIRE_MACE", 87);
    feat_improved_critical_double_axe = cr->add("FEAT_IMPROVED_CRITICAL_DOUBLE_AXE", 88);
    feat_improved_critical_two_bladed_sword = cr->add("FEAT_IMPROVED_CRITICAL_TWO_BLADED_SWORD", 89);
    feat_weapon_focus_dagger = cr->add("FEAT_WEAPON_FOCUS_DAGGER", 90);
    feat_weapon_focus_dart = cr->add("FEAT_WEAPON_FOCUS_DART", 91);
    feat_weapon_focus_heavy_crossbow = cr->add("FEAT_WEAPON_FOCUS_HEAVY_CROSSBOW", 92);
    feat_weapon_focus_light_crossbow = cr->add("FEAT_WEAPON_FOCUS_LIGHT_CROSSBOW", 93);
    feat_weapon_focus_light_mace = cr->add("FEAT_WEAPON_FOCUS_LIGHT_MACE", 94);
    feat_weapon_focus_morning_star = cr->add("FEAT_WEAPON_FOCUS_MORNING_STAR", 95);
    feat_weapon_focus_staff = cr->add("FEAT_WEAPON_FOCUS_STAFF", 96);
    feat_weapon_focus_spear = cr->add("FEAT_WEAPON_FOCUS_SPEAR", 97);
    feat_weapon_focus_sickle = cr->add("FEAT_WEAPON_FOCUS_SICKLE", 98);
    feat_weapon_focus_sling = cr->add("FEAT_WEAPON_FOCUS_SLING", 99);
    feat_weapon_focus_unarmed_strike = cr->add("FEAT_WEAPON_FOCUS_UNARMED_STRIKE", 100);
    feat_weapon_focus_longbow = cr->add("FEAT_WEAPON_FOCUS_LONGBOW", 101);
    feat_weapon_focus_shortbow = cr->add("FEAT_WEAPON_FOCUS_SHORTBOW", 102);
    feat_weapon_focus_short_sword = cr->add("FEAT_WEAPON_FOCUS_SHORT_SWORD", 103);
    feat_weapon_focus_rapier = cr->add("FEAT_WEAPON_FOCUS_RAPIER", 104);
    feat_weapon_focus_scimitar = cr->add("FEAT_WEAPON_FOCUS_SCIMITAR", 105);
    feat_weapon_focus_long_sword = cr->add("FEAT_WEAPON_FOCUS_LONG_SWORD", 106);
    feat_weapon_focus_great_sword = cr->add("FEAT_WEAPON_FOCUS_GREAT_SWORD", 107);
    feat_weapon_focus_hand_axe = cr->add("FEAT_WEAPON_FOCUS_HAND_AXE", 108);
    feat_weapon_focus_throwing_axe = cr->add("FEAT_WEAPON_FOCUS_THROWING_AXE", 109);
    feat_weapon_focus_battle_axe = cr->add("FEAT_WEAPON_FOCUS_BATTLE_AXE", 110);
    feat_weapon_focus_great_axe = cr->add("FEAT_WEAPON_FOCUS_GREAT_AXE", 111);
    feat_weapon_focus_halberd = cr->add("FEAT_WEAPON_FOCUS_HALBERD", 112);
    feat_weapon_focus_light_hammer = cr->add("FEAT_WEAPON_FOCUS_LIGHT_HAMMER", 113);
    feat_weapon_focus_light_flail = cr->add("FEAT_WEAPON_FOCUS_LIGHT_FLAIL", 114);
    feat_weapon_focus_war_hammer = cr->add("FEAT_WEAPON_FOCUS_WAR_HAMMER", 115);
    feat_weapon_focus_heavy_flail = cr->add("FEAT_WEAPON_FOCUS_HEAVY_FLAIL", 116);
    feat_weapon_focus_kama = cr->add("FEAT_WEAPON_FOCUS_KAMA", 117);
    feat_weapon_focus_kukri = cr->add("FEAT_WEAPON_FOCUS_KUKRI", 118);
    // feat_weapon_focus_nunchaku = cr->add( "FEAT_WEAPON_FOCUS_NUNCHAKU" , 119);
    feat_weapon_focus_shuriken = cr->add("FEAT_WEAPON_FOCUS_SHURIKEN", 120);
    feat_weapon_focus_scythe = cr->add("FEAT_WEAPON_FOCUS_SCYTHE", 121);
    feat_weapon_focus_katana = cr->add("FEAT_WEAPON_FOCUS_KATANA", 122);
    feat_weapon_focus_bastard_sword = cr->add("FEAT_WEAPON_FOCUS_BASTARD_SWORD", 123);
    feat_weapon_focus_dire_mace = cr->add("FEAT_WEAPON_FOCUS_DIRE_MACE", 125);
    feat_weapon_focus_double_axe = cr->add("FEAT_WEAPON_FOCUS_DOUBLE_AXE", 126);
    feat_weapon_focus_two_bladed_sword = cr->add("FEAT_WEAPON_FOCUS_TWO_BLADED_SWORD", 127);
    feat_weapon_specialization_dagger = cr->add("FEAT_WEAPON_SPECIALIZATION_DAGGER", 128);
    feat_weapon_specialization_dart = cr->add("FEAT_WEAPON_SPECIALIZATION_DART", 129);
    feat_weapon_specialization_heavy_crossbow = cr->add("FEAT_WEAPON_SPECIALIZATION_HEAVY_CROSSBOW", 130);
    feat_weapon_specialization_light_crossbow = cr->add("FEAT_WEAPON_SPECIALIZATION_LIGHT_CROSSBOW", 131);
    feat_weapon_specialization_light_mace = cr->add("FEAT_WEAPON_SPECIALIZATION_LIGHT_MACE", 132);
    feat_weapon_specialization_morning_star = cr->add("FEAT_WEAPON_SPECIALIZATION_MORNING_STAR", 133);
    feat_weapon_specialization_staff = cr->add("FEAT_WEAPON_SPECIALIZATION_STAFF", 134);
    feat_weapon_specialization_spear = cr->add("FEAT_WEAPON_SPECIALIZATION_SPEAR", 135);
    feat_weapon_specialization_sickle = cr->add("FEAT_WEAPON_SPECIALIZATION_SICKLE", 136);
    feat_weapon_specialization_sling = cr->add("FEAT_WEAPON_SPECIALIZATION_SLING", 137);
    feat_weapon_specialization_unarmed_strike = cr->add("FEAT_WEAPON_SPECIALIZATION_UNARMED_STRIKE", 138);
    feat_weapon_specialization_longbow = cr->add("FEAT_WEAPON_SPECIALIZATION_LONGBOW", 139);
    feat_weapon_specialization_shortbow = cr->add("FEAT_WEAPON_SPECIALIZATION_SHORTBOW", 140);
    feat_weapon_specialization_short_sword = cr->add("FEAT_WEAPON_SPECIALIZATION_SHORT_SWORD", 141);
    feat_weapon_specialization_rapier = cr->add("FEAT_WEAPON_SPECIALIZATION_RAPIER", 142);
    feat_weapon_specialization_scimitar = cr->add("FEAT_WEAPON_SPECIALIZATION_SCIMITAR", 143);
    feat_weapon_specialization_long_sword = cr->add("FEAT_WEAPON_SPECIALIZATION_LONG_SWORD", 144);
    feat_weapon_specialization_great_sword = cr->add("FEAT_WEAPON_SPECIALIZATION_GREAT_SWORD", 145);
    feat_weapon_specialization_hand_axe = cr->add("FEAT_WEAPON_SPECIALIZATION_HAND_AXE", 146);
    feat_weapon_specialization_throwing_axe = cr->add("FEAT_WEAPON_SPECIALIZATION_THROWING_AXE", 147);
    feat_weapon_specialization_battle_axe = cr->add("FEAT_WEAPON_SPECIALIZATION_BATTLE_AXE", 148);
    feat_weapon_specialization_great_axe = cr->add("FEAT_WEAPON_SPECIALIZATION_GREAT_AXE", 149);
    feat_weapon_specialization_halberd = cr->add("FEAT_WEAPON_SPECIALIZATION_HALBERD", 150);
    feat_weapon_specialization_light_hammer = cr->add("FEAT_WEAPON_SPECIALIZATION_LIGHT_HAMMER", 151);
    feat_weapon_specialization_light_flail = cr->add("FEAT_WEAPON_SPECIALIZATION_LIGHT_FLAIL", 152);
    feat_weapon_specialization_war_hammer = cr->add("FEAT_WEAPON_SPECIALIZATION_WAR_HAMMER", 153);
    feat_weapon_specialization_heavy_flail = cr->add("FEAT_WEAPON_SPECIALIZATION_HEAVY_FLAIL", 154);
    feat_weapon_specialization_kama = cr->add("FEAT_WEAPON_SPECIALIZATION_KAMA", 155);
    feat_weapon_specialization_kukri = cr->add("FEAT_WEAPON_SPECIALIZATION_KUKRI", 156);
    // feat_weapon_specialization_nunchaku = cr->add( "FEAT_WEAPON_SPECIALIZATION_NUNCHAKU" , 157);
    feat_weapon_specialization_shuriken = cr->add("FEAT_WEAPON_SPECIALIZATION_SHURIKEN", 158);
    feat_weapon_specialization_scythe = cr->add("FEAT_WEAPON_SPECIALIZATION_SCYTHE", 159);
    feat_weapon_specialization_katana = cr->add("FEAT_WEAPON_SPECIALIZATION_KATANA", 160);
    feat_weapon_specialization_bastard_sword = cr->add("FEAT_WEAPON_SPECIALIZATION_BASTARD_SWORD", 161);
    feat_weapon_specialization_dire_mace = cr->add("FEAT_WEAPON_SPECIALIZATION_DIRE_MACE", 163);
    feat_weapon_specialization_double_axe = cr->add("FEAT_WEAPON_SPECIALIZATION_DOUBLE_AXE", 164);
    feat_weapon_specialization_two_bladed_sword = cr->add("FEAT_WEAPON_SPECIALIZATION_TWO_BLADED_SWORD", 165);
    feat_spell_focus_conjuration = cr->add("FEAT_SPELL_FOCUS_CONJURATION", 166);
    feat_spell_focus_divination = cr->add("FEAT_SPELL_FOCUS_DIVINATION", 167);
    feat_spell_focus_enchantment = cr->add("FEAT_SPELL_FOCUS_ENCHANTMENT", 168);
    feat_spell_focus_evocation = cr->add("FEAT_SPELL_FOCUS_EVOCATION", 169);
    feat_spell_focus_illusion = cr->add("FEAT_SPELL_FOCUS_ILLUSION", 170);
    feat_spell_focus_necromancy = cr->add("FEAT_SPELL_FOCUS_NECROMANCY", 171);
    feat_spell_focus_transmutation = cr->add("FEAT_SPELL_FOCUS_TRANSMUTATION", 172);
    feat_skill_focus_concentration = cr->add("FEAT_SKILL_FOCUS_CONCENTRATION", 173);
    feat_skill_focus_disable_trap = cr->add("FEAT_SKILL_FOCUS_DISABLE_TRAP", 174);
    feat_skill_focus_discipline = cr->add("FEAT_SKILL_FOCUS_DISCIPLINE", 175);
    feat_skill_focus_heal = cr->add("FEAT_SKILL_FOCUS_HEAL", 177);
    feat_skill_focus_hide = cr->add("FEAT_SKILL_FOCUS_HIDE", 178);
    feat_skill_focus_listen = cr->add("FEAT_SKILL_FOCUS_LISTEN", 179);
    feat_skill_focus_lore = cr->add("FEAT_SKILL_FOCUS_LORE", 180);
    feat_skill_focus_move_silently = cr->add("FEAT_SKILL_FOCUS_MOVE_SILENTLY", 181);
    feat_skill_focus_open_lock = cr->add("FEAT_SKILL_FOCUS_OPEN_LOCK", 182);
    feat_skill_focus_parry = cr->add("FEAT_SKILL_FOCUS_PARRY", 183);
    feat_skill_focus_perform = cr->add("FEAT_SKILL_FOCUS_PERFORM", 184);
    feat_skill_focus_persuade = cr->add("FEAT_SKILL_FOCUS_PERSUADE", 185);
    feat_skill_focus_pick_pocket = cr->add("FEAT_SKILL_FOCUS_PICK_POCKET", 186);
    feat_skill_focus_search = cr->add("FEAT_SKILL_FOCUS_SEARCH", 187);
    feat_skill_focus_set_trap = cr->add("FEAT_SKILL_FOCUS_SET_TRAP", 188);
    feat_skill_focus_spellcraft = cr->add("FEAT_SKILL_FOCUS_SPELLCRAFT", 189);
    feat_skill_focus_spot = cr->add("FEAT_SKILL_FOCUS_SPOT", 190);
    feat_skill_focus_taunt = cr->add("FEAT_SKILL_FOCUS_TAUNT", 192);
    feat_skill_focus_use_magic_device = cr->add("FEAT_SKILL_FOCUS_USE_MAGIC_DEVICE", 193);
    feat_barbarian_endurance = cr->add("FEAT_BARBARIAN_ENDURANCE", 194);
    feat_uncanny_dodge_1 = cr->add("FEAT_UNCANNY_DODGE_1", 195);
    feat_damage_reduction = cr->add("FEAT_DAMAGE_REDUCTION", 196);
    feat_bardic_knowledge = cr->add("FEAT_BARDIC_KNOWLEDGE", 197);
    feat_nature_sense = cr->add("FEAT_NATURE_SENSE", 198);
    feat_animal_companion = cr->add("FEAT_ANIMAL_COMPANION", 199);
    feat_woodland_stride = cr->add("FEAT_WOODLAND_STRIDE", 200);
    feat_trackless_step = cr->add("FEAT_TRACKLESS_STEP", 201);
    feat_resist_natures_lure = cr->add("FEAT_RESIST_NATURES_LURE", 202);
    feat_venom_immunity = cr->add("FEAT_VENOM_IMMUNITY", 203);
    feat_flurry_of_blows = cr->add("FEAT_FLURRY_OF_BLOWS", 204);
    feat_evasion = cr->add("FEAT_EVASION", 206);
    feat_monk_endurance = cr->add("FEAT_MONK_ENDURANCE", 207);
    feat_still_mind = cr->add("FEAT_STILL_MIND", 208);
    feat_purity_of_body = cr->add("FEAT_PURITY_OF_BODY", 209);
    feat_wholeness_of_body = cr->add("FEAT_WHOLENESS_OF_BODY", 211);
    feat_improved_evasion = cr->add("FEAT_IMPROVED_EVASION", 212);
    feat_ki_strike = cr->add("FEAT_KI_STRIKE", 213);
    feat_diamond_body = cr->add("FEAT_DIAMOND_BODY", 214);
    feat_diamond_soul = cr->add("FEAT_DIAMOND_SOUL", 215);
    feat_perfect_self = cr->add("FEAT_PERFECT_SELF", 216);
    feat_divine_grace = cr->add("FEAT_DIVINE_GRACE", 217);
    feat_divine_health = cr->add("FEAT_DIVINE_HEALTH", 219);
    feat_sneak_attack = cr->add("FEAT_SNEAK_ATTACK", 221);
    feat_crippling_strike = cr->add("FEAT_CRIPPLING_STRIKE", 222);
    feat_defensive_roll = cr->add("FEAT_DEFENSIVE_ROLL", 223);
    feat_opportunist = cr->add("FEAT_OPPORTUNIST", 224);
    feat_skill_mastery = cr->add("FEAT_SKILL_MASTERY", 225);
    feat_uncanny_reflex = cr->add("FEAT_UNCANNY_REFLEX", 226);
    feat_stonecunning = cr->add("FEAT_STONECUNNING", 227);
    feat_darkvision = cr->add("FEAT_DARKVISION", 228);
    feat_hardiness_versus_poisons = cr->add("FEAT_HARDINESS_VERSUS_POISONS", 229);
    feat_hardiness_versus_spells = cr->add("FEAT_HARDINESS_VERSUS_SPELLS", 230);
    feat_battle_training_versus_orcs = cr->add("FEAT_BATTLE_TRAINING_VERSUS_ORCS", 231);
    feat_battle_training_versus_goblins = cr->add("FEAT_BATTLE_TRAINING_VERSUS_GOBLINS", 232);
    feat_battle_training_versus_giants = cr->add("FEAT_BATTLE_TRAINING_VERSUS_GIANTS", 233);
    feat_skill_affinity_lore = cr->add("FEAT_SKILL_AFFINITY_LORE", 234);
    feat_immunity_to_sleep = cr->add("FEAT_IMMUNITY_TO_SLEEP", 235);
    feat_hardiness_versus_enchantments = cr->add("FEAT_HARDINESS_VERSUS_ENCHANTMENTS", 236);
    feat_skill_affinity_listen = cr->add("FEAT_SKILL_AFFINITY_LISTEN", 237);
    feat_skill_affinity_search = cr->add("FEAT_SKILL_AFFINITY_SEARCH", 238);
    feat_skill_affinity_spot = cr->add("FEAT_SKILL_AFFINITY_SPOT", 239);
    feat_keen_sense = cr->add("FEAT_KEEN_SENSE", 240);
    feat_hardiness_versus_illusions = cr->add("FEAT_HARDINESS_VERSUS_ILLUSIONS", 241);
    feat_battle_training_versus_reptilians = cr->add("FEAT_BATTLE_TRAINING_VERSUS_REPTILIANS", 242);
    feat_skill_affinity_concentration = cr->add("FEAT_SKILL_AFFINITY_CONCENTRATION", 243);
    feat_partial_skill_affinity_listen = cr->add("FEAT_PARTIAL_SKILL_AFFINITY_LISTEN", 244);
    feat_partial_skill_affinity_search = cr->add("FEAT_PARTIAL_SKILL_AFFINITY_SEARCH", 245);
    feat_partial_skill_affinity_spot = cr->add("FEAT_PARTIAL_SKILL_AFFINITY_SPOT", 246);
    feat_skill_affinity_move_silently = cr->add("FEAT_SKILL_AFFINITY_MOVE_SILENTLY", 247);
    feat_lucky = cr->add("FEAT_LUCKY", 248);
    feat_fearless = cr->add("FEAT_FEARLESS", 249);
    feat_good_aim = cr->add("FEAT_GOOD_AIM", 250);
    feat_uncanny_dodge_2 = cr->add("FEAT_UNCANNY_DODGE_2", 251);
    feat_uncanny_dodge_3 = cr->add("FEAT_UNCANNY_DODGE_3", 252);
    feat_uncanny_dodge_4 = cr->add("FEAT_UNCANNY_DODGE_4", 253);
    feat_uncanny_dodge_5 = cr->add("FEAT_UNCANNY_DODGE_5", 254);
    feat_uncanny_dodge_6 = cr->add("FEAT_UNCANNY_DODGE_6", 255);
    feat_weapon_proficiency_elf = cr->add("FEAT_WEAPON_PROFICIENCY_ELF", 256);
    feat_bard_songs = cr->add("FEAT_BARD_SONGS", 257);
    feat_quick_to_master = cr->add("FEAT_QUICK_TO_MASTER", 258);
    feat_slippery_mind = cr->add("FEAT_SLIPPERY_MIND", 259);
    feat_monk_ac_bonus = cr->add("FEAT_MONK_AC_BONUS", 260);
    feat_favored_enemy_dwarf = cr->add("FEAT_FAVORED_ENEMY_DWARF", 261);
    feat_favored_enemy_elf = cr->add("FEAT_FAVORED_ENEMY_ELF", 262);
    feat_favored_enemy_gnome = cr->add("FEAT_FAVORED_ENEMY_GNOME", 263);
    feat_favored_enemy_halfling = cr->add("FEAT_FAVORED_ENEMY_HALFLING", 264);
    feat_favored_enemy_halfelf = cr->add("FEAT_FAVORED_ENEMY_HALFELF", 265);
    feat_favored_enemy_halforc = cr->add("FEAT_FAVORED_ENEMY_HALFORC", 266);
    feat_favored_enemy_human = cr->add("FEAT_FAVORED_ENEMY_HUMAN", 267);
    feat_favored_enemy_aberration = cr->add("FEAT_FAVORED_ENEMY_ABERRATION", 268);
    feat_favored_enemy_animal = cr->add("FEAT_FAVORED_ENEMY_ANIMAL", 269);
    feat_favored_enemy_beast = cr->add("FEAT_FAVORED_ENEMY_BEAST", 270);
    feat_favored_enemy_construct = cr->add("FEAT_FAVORED_ENEMY_CONSTRUCT", 271);
    feat_favored_enemy_dragon = cr->add("FEAT_FAVORED_ENEMY_DRAGON", 272);
    feat_favored_enemy_goblinoid = cr->add("FEAT_FAVORED_ENEMY_GOBLINOID", 273);
    feat_favored_enemy_monstrous = cr->add("FEAT_FAVORED_ENEMY_MONSTROUS", 274);
    feat_favored_enemy_orc = cr->add("FEAT_FAVORED_ENEMY_ORC", 275);
    feat_favored_enemy_reptilian = cr->add("FEAT_FAVORED_ENEMY_REPTILIAN", 276);
    feat_favored_enemy_elemental = cr->add("FEAT_FAVORED_ENEMY_ELEMENTAL", 277);
    feat_favored_enemy_fey = cr->add("FEAT_FAVORED_ENEMY_FEY", 278);
    feat_favored_enemy_giant = cr->add("FEAT_FAVORED_ENEMY_GIANT", 279);
    feat_favored_enemy_magical_beast = cr->add("FEAT_FAVORED_ENEMY_MAGICAL_BEAST", 280);
    feat_favored_enemy_outsider = cr->add("FEAT_FAVORED_ENEMY_OUTSIDER", 281);
    feat_favored_enemy_shapechanger = cr->add("FEAT_FAVORED_ENEMY_SHAPECHANGER", 284);
    feat_favored_enemy_undead = cr->add("FEAT_FAVORED_ENEMY_UNDEAD", 285);
    feat_favored_enemy_vermin = cr->add("FEAT_FAVORED_ENEMY_VERMIN", 286);
    feat_weapon_proficiency_creature = cr->add("FEAT_WEAPON_PROFICIENCY_CREATURE", 289);
    feat_weapon_specialization_creature = cr->add("FEAT_WEAPON_SPECIALIZATION_CREATURE", 290);
    feat_weapon_focus_creature = cr->add("FEAT_WEAPON_FOCUS_CREATURE", 291);
    feat_improved_critical_creature = cr->add("FEAT_IMPROVED_CRITICAL_CREATURE", 292);
    feat_barbarian_rage = cr->add("FEAT_BARBARIAN_RAGE", 293);
    feat_turn_undead = cr->add("FEAT_TURN_UNDEAD", 294);
    feat_quivering_palm = cr->add("FEAT_QUIVERING_PALM", 296);
    feat_empty_body = cr->add("FEAT_EMPTY_BODY", 297);
    // feat_detect_evil = cr->add( "FEAT_DETECT_EVIL" , 298);
    feat_lay_on_hands = cr->add("FEAT_LAY_ON_HANDS", 299);
    feat_aura_of_courage = cr->add("FEAT_AURA_OF_COURAGE", 300);
    feat_smite_evil = cr->add("FEAT_SMITE_EVIL", 301);
    feat_remove_disease = cr->add("FEAT_REMOVE_DISEASE", 302);
    feat_summon_familiar = cr->add("FEAT_SUMMON_FAMILIAR", 303);
    feat_elemental_shape = cr->add("FEAT_ELEMENTAL_SHAPE", 304);
    feat_wild_shape = cr->add("FEAT_WILD_SHAPE", 305);
    feat_war_domain_power = cr->add("FEAT_WAR_DOMAIN_POWER", 306);
    feat_strength_domain_power = cr->add("FEAT_STRENGTH_DOMAIN_POWER", 307);
    feat_protection_domain_power = cr->add("FEAT_PROTECTION_DOMAIN_POWER", 308);
    feat_luck_domain_power = cr->add("FEAT_LUCK_DOMAIN_POWER", 309);
    feat_death_domain_power = cr->add("FEAT_DEATH_DOMAIN_POWER", 310);
    feat_air_domain_power = cr->add("FEAT_AIR_DOMAIN_POWER", 311);
    feat_animal_domain_power = cr->add("FEAT_ANIMAL_DOMAIN_POWER", 312);
    feat_destruction_domain_power = cr->add("FEAT_DESTRUCTION_DOMAIN_POWER", 313);
    feat_earth_domain_power = cr->add("FEAT_EARTH_DOMAIN_POWER", 314);
    feat_evil_domain_power = cr->add("FEAT_EVIL_DOMAIN_POWER", 315);
    feat_fire_domain_power = cr->add("FEAT_FIRE_DOMAIN_POWER", 316);
    feat_good_domain_power = cr->add("FEAT_GOOD_DOMAIN_POWER", 317);
    feat_healing_domain_power = cr->add("FEAT_HEALING_DOMAIN_POWER", 318);
    feat_knowledge_domain_power = cr->add("FEAT_KNOWLEDGE_DOMAIN_POWER", 319);
    feat_magic_domain_power = cr->add("FEAT_MAGIC_DOMAIN_POWER", 320);
    feat_plant_domain_power = cr->add("FEAT_PLANT_DOMAIN_POWER", 321);
    feat_sun_domain_power = cr->add("FEAT_SUN_DOMAIN_POWER", 322);
    feat_travel_domain_power = cr->add("FEAT_TRAVEL_DOMAIN_POWER", 323);
    feat_trickery_domain_power = cr->add("FEAT_TRICKERY_DOMAIN_POWER", 324);
    feat_water_domain_power = cr->add("FEAT_WATER_DOMAIN_POWER", 325);
    feat_lowlightvision = cr->add("FEAT_LOWLIGHTVISION", 354);
    feat_improved_initiative = cr->add("FEAT_IMPROVED_INITIATIVE", 377);
    feat_artist = cr->add("FEAT_ARTIST", 378);
    feat_blooded = cr->add("FEAT_BLOODED", 379);
    feat_bullheaded = cr->add("FEAT_BULLHEADED", 380);
    feat_courtly_magocracy = cr->add("FEAT_COURTLY_MAGOCRACY", 381);
    feat_luck_of_heroes = cr->add("FEAT_LUCK_OF_HEROES", 382);
    feat_resist_poison = cr->add("FEAT_RESIST_POISON", 383);
    feat_silver_palm = cr->add("FEAT_SILVER_PALM", 384);
    feat_snakeblood = cr->add("FEAT_SNAKEBLOOD", 386);
    feat_stealthy = cr->add("FEAT_STEALTHY", 387);
    feat_strongsoul = cr->add("FEAT_STRONGSOUL", 388);
    feat_expertise = cr->add("FEAT_EXPERTISE", 389);
    feat_improved_expertise = cr->add("FEAT_IMPROVED_EXPERTISE", 390);
    feat_great_cleave = cr->add("FEAT_GREAT_CLEAVE", 391);
    feat_spring_attack = cr->add("FEAT_SPRING_ATTACK", 392);
    feat_greater_spell_focus_abjuration = cr->add("FEAT_GREATER_SPELL_FOCUS_ABJURATION", 393);
    feat_greater_spell_focus_conjuration = cr->add("FEAT_GREATER_SPELL_FOCUS_CONJURATION", 394);
    feat_greater_spell_focus_diviniation = cr->add("FEAT_GREATER_SPELL_FOCUS_DIVINIATION", 395);
    feat_greater_spell_focus_divination = cr->add("FEAT_GREATER_SPELL_FOCUS_DIVINATION", 395);
    feat_greater_spell_focus_enchantment = cr->add("FEAT_GREATER_SPELL_FOCUS_ENCHANTMENT", 396);
    feat_greater_spell_focus_evocation = cr->add("FEAT_GREATER_SPELL_FOCUS_EVOCATION", 397);
    feat_greater_spell_focus_illusion = cr->add("FEAT_GREATER_SPELL_FOCUS_ILLUSION", 398);
    feat_greater_spell_focus_necromancy = cr->add("FEAT_GREATER_SPELL_FOCUS_NECROMANCY", 399);
    feat_greater_spell_focus_transmutation = cr->add("FEAT_GREATER_SPELL_FOCUS_TRANSMUTATION", 400);
    feat_greater_spell_penetration = cr->add("FEAT_GREATER_SPELL_PENETRATION", 401);
    feat_thug = cr->add("FEAT_THUG", 402);
    feat_skillfocus_appraise = cr->add("FEAT_SKILLFOCUS_APPRAISE", 404);
    feat_skill_focus_tumble = cr->add("FEAT_SKILL_FOCUS_TUMBLE", 406);
    feat_skill_focus_craft_trap = cr->add("FEAT_SKILL_FOCUS_CRAFT_TRAP", 407);
    feat_blind_fight = cr->add("FEAT_BLIND_FIGHT", 408);
    feat_circle_kick = cr->add("FEAT_CIRCLE_KICK", 409);
    feat_extra_stunning_attack = cr->add("FEAT_EXTRA_STUNNING_ATTACK", 410);
    feat_rapid_reload = cr->add("FEAT_RAPID_RELOAD", 411);
    feat_zen_archery = cr->add("FEAT_ZEN_ARCHERY", 412);
    feat_divine_might = cr->add("FEAT_DIVINE_MIGHT", 413);
    feat_divine_shield = cr->add("FEAT_DIVINE_SHIELD", 414);
    feat_arcane_defense_abjuration = cr->add("FEAT_ARCANE_DEFENSE_ABJURATION", 415);
    feat_arcane_defense_conjuration = cr->add("FEAT_ARCANE_DEFENSE_CONJURATION", 416);
    feat_arcane_defense_divination = cr->add("FEAT_ARCANE_DEFENSE_DIVINATION", 417);
    feat_arcane_defense_enchantment = cr->add("FEAT_ARCANE_DEFENSE_ENCHANTMENT", 418);
    feat_arcane_defense_evocation = cr->add("FEAT_ARCANE_DEFENSE_EVOCATION", 419);
    feat_arcane_defense_illusion = cr->add("FEAT_ARCANE_DEFENSE_ILLUSION", 420);
    feat_arcane_defense_necromancy = cr->add("FEAT_ARCANE_DEFENSE_NECROMANCY", 421);
    feat_arcane_defense_transmutation = cr->add("FEAT_ARCANE_DEFENSE_TRANSMUTATION", 422);
    feat_extra_music = cr->add("FEAT_EXTRA_MUSIC", 423);
    feat_lingering_song = cr->add("FEAT_LINGERING_SONG", 424);
    feat_dirty_fighting = cr->add("FEAT_DIRTY_FIGHTING", 425);
    feat_resist_disease = cr->add("FEAT_RESIST_DISEASE", 426);
    feat_resist_energy_cold = cr->add("FEAT_RESIST_ENERGY_COLD", 427);
    feat_resist_energy_acid = cr->add("FEAT_RESIST_ENERGY_ACID", 428);
    feat_resist_energy_fire = cr->add("FEAT_RESIST_ENERGY_FIRE", 429);
    feat_resist_energy_electrical = cr->add("FEAT_RESIST_ENERGY_ELECTRICAL", 430);
    feat_resist_energy_sonic = cr->add("FEAT_RESIST_ENERGY_SONIC", 431);
    feat_hide_in_plain_sight = cr->add("FEAT_HIDE_IN_PLAIN_SIGHT", 433);
    feat_shadow_daze = cr->add("FEAT_SHADOW_DAZE", 434);
    feat_summon_shadow = cr->add("FEAT_SUMMON_SHADOW", 435);
    feat_shadow_evade = cr->add("FEAT_SHADOW_EVADE", 436);
    feat_deneirs_eye = cr->add("FEAT_DENEIRS_EYE", 437);
    feat_tymoras_smile = cr->add("FEAT_TYMORAS_SMILE", 438);
    feat_lliiras_heart = cr->add("FEAT_LLIIRAS_HEART", 439);
    feat_craft_harper_item = cr->add("FEAT_CRAFT_HARPER_ITEM", 440);
    feat_harper_sleep = cr->add("FEAT_HARPER_SLEEP", 441);
    feat_harper_cats_grace = cr->add("FEAT_HARPER_CATS_GRACE", 442);
    feat_harper_eagles_splendor = cr->add("FEAT_HARPER_EAGLES_SPLENDOR", 443);
    feat_harper_invisibility = cr->add("FEAT_HARPER_INVISIBILITY", 444);

    feat_prestige_enchant_arrow_1 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_1", 445);

    feat_prestige_enchant_arrow_2 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_2", 446);
    feat_prestige_enchant_arrow_3 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_3", 447);
    feat_prestige_enchant_arrow_4 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_4", 448);
    feat_prestige_enchant_arrow_5 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_5", 449);
    feat_prestige_imbue_arrow = cr->add("FEAT_PRESTIGE_IMBUE_ARROW", 450);
    feat_prestige_seeker_arrow_1 = cr->add("FEAT_PRESTIGE_SEEKER_ARROW_1", 451);
    feat_prestige_seeker_arrow_2 = cr->add("FEAT_PRESTIGE_SEEKER_ARROW_2", 452);
    feat_prestige_hail_of_arrows = cr->add("FEAT_PRESTIGE_HAIL_OF_ARROWS", 453);
    feat_prestige_arrow_of_death = cr->add("FEAT_PRESTIGE_ARROW_OF_DEATH", 454);

    feat_prestige_death_attack_1 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_1", 455);
    feat_prestige_death_attack_2 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_2", 456);
    feat_prestige_death_attack_3 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_3", 457);
    feat_prestige_death_attack_4 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_4", 458);
    feat_prestige_death_attack_5 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_5", 459);

    feat_blackguard_sneak_attack_1d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_1D6", 460);
    feat_blackguard_sneak_attack_2d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_2D6", 461);
    feat_blackguard_sneak_attack_3d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_3D6", 462);

    feat_prestige_poison_save_1 = cr->add("FEAT_PRESTIGE_POISON_SAVE_1", 463);
    feat_prestige_poison_save_2 = cr->add("FEAT_PRESTIGE_POISON_SAVE_2", 464);
    feat_prestige_poison_save_3 = cr->add("FEAT_PRESTIGE_POISON_SAVE_3", 465);
    feat_prestige_poison_save_4 = cr->add("FEAT_PRESTIGE_POISON_SAVE_4", 466);
    feat_prestige_poison_save_5 = cr->add("FEAT_PRESTIGE_POISON_SAVE_5", 467);

    feat_prestige_spell_ghostly_visage = cr->add("FEAT_PRESTIGE_SPELL_GHOSTLY_VISAGE", 468);
    feat_prestige_darkness = cr->add("FEAT_PRESTIGE_DARKNESS", 469);
    feat_prestige_invisibility_1 = cr->add("FEAT_PRESTIGE_INVISIBILITY_1", 470);
    feat_prestige_invisibility_2 = cr->add("FEAT_PRESTIGE_INVISIBILITY_2", 471);

    feat_smite_good = cr->add("FEAT_SMITE_GOOD", 472);

    feat_prestige_dark_blessing = cr->add("FEAT_PRESTIGE_DARK_BLESSING", 473);
    feat_inflict_light_wounds = cr->add("FEAT_INFLICT_LIGHT_WOUNDS", 474);
    feat_inflict_moderate_wounds = cr->add("FEAT_INFLICT_MODERATE_WOUNDS", 475);
    feat_inflict_serious_wounds = cr->add("FEAT_INFLICT_SERIOUS_WOUNDS", 476);
    feat_inflict_critical_wounds = cr->add("FEAT_INFLICT_CRITICAL_WOUNDS", 477);
    feat_bulls_strength = cr->add("FEAT_BULLS_STRENGTH", 478);
    feat_contagion = cr->add("FEAT_CONTAGION", 479);
    feat_eye_of_gruumsh_blinding_spittle = cr->add("FEAT_EYE_OF_GRUUMSH_BLINDING_SPITTLE", 480);
    feat_eye_of_gruumsh_blinding_spittle_2 = cr->add("FEAT_EYE_OF_GRUUMSH_BLINDING_SPITTLE_2", 481);
    feat_eye_of_gruumsh_command_the_horde = cr->add("FEAT_EYE_OF_GRUUMSH_COMMAND_THE_HORDE", 482);
    feat_eye_of_gruumsh_swing_blindly = cr->add("FEAT_EYE_OF_GRUUMSH_SWING_BLINDLY", 483);
    feat_eye_of_gruumsh_ritual_scarring = cr->add("FEAT_EYE_OF_GRUUMSH_RITUAL_SCARRING", 484);
    feat_blindsight_5_feet = cr->add("FEAT_BLINDSIGHT_5_FEET", 485);
    feat_blindsight_10_feet = cr->add("FEAT_BLINDSIGHT_10_FEET", 486);
    feat_eye_of_gruumsh_sight_of_gruumsh = cr->add("FEAT_EYE_OF_GRUUMSH_SIGHT_OF_GRUUMSH", 487);
    feat_blindsight_60_feet = cr->add("FEAT_BLINDSIGHT_60_FEET", 488);
    feat_shou_disciple_dodge_2 = cr->add("FEAT_SHOU_DISCIPLE_DODGE_2", 489);
    feat_epic_armor_skin = cr->add("FEAT_EPIC_ARMOR_SKIN", 490);
    feat_epic_blinding_speed = cr->add("FEAT_EPIC_BLINDING_SPEED", 491);
    feat_epic_damage_reduction_3 = cr->add("FEAT_EPIC_DAMAGE_REDUCTION_3", 492);
    feat_epic_damage_reduction_6 = cr->add("FEAT_EPIC_DAMAGE_REDUCTION_6", 493);
    feat_epic_damage_reduction_9 = cr->add("FEAT_EPIC_DAMAGE_REDUCTION_9", 494);
    feat_epic_devastating_critical_club = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_CLUB", 495);
    feat_epic_devastating_critical_dagger = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_DAGGER", 496);
    feat_epic_devastating_critical_dart = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_DART", 497);
    feat_epic_devastating_critical_heavycrossbow = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_HEAVYCROSSBOW", 498);
    feat_epic_devastating_critical_lightcrossbow = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTCROSSBOW", 499);
    feat_epic_devastating_critical_lightmace = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTMACE", 500);
    feat_epic_devastating_critical_morningstar = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_MORNINGSTAR", 501);
    feat_epic_devastating_critical_quarterstaff = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_QUARTERSTAFF", 502);
    feat_epic_devastating_critical_shortspear = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SHORTSPEAR", 503);
    feat_epic_devastating_critical_sickle = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SICKLE", 504);
    feat_epic_devastating_critical_sling = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SLING", 505);
    feat_epic_devastating_critical_unarmed = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_UNARMED", 506);
    feat_epic_devastating_critical_longbow = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_LONGBOW", 507);
    feat_epic_devastating_critical_shortbow = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SHORTBOW", 508);
    feat_epic_devastating_critical_shortsword = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SHORTSWORD", 509);
    feat_epic_devastating_critical_rapier = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_RAPIER", 510);
    feat_epic_devastating_critical_scimitar = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SCIMITAR", 511);
    feat_epic_devastating_critical_longsword = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_LONGSWORD", 512);
    feat_epic_devastating_critical_greatsword = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_GREATSWORD", 513);
    feat_epic_devastating_critical_handaxe = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_HANDAXE", 514);
    feat_epic_devastating_critical_throwingaxe = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_THROWINGAXE", 515);
    feat_epic_devastating_critical_battleaxe = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_BATTLEAXE", 516);
    feat_epic_devastating_critical_greataxe = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_GREATAXE", 517);
    feat_epic_devastating_critical_halberd = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_HALBERD", 518);
    feat_epic_devastating_critical_lighthammer = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTHAMMER", 519);
    feat_epic_devastating_critical_lightflail = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_LIGHTFLAIL", 520);
    feat_epic_devastating_critical_warhammer = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_WARHAMMER", 521);
    feat_epic_devastating_critical_heavyflail = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_HEAVYFLAIL", 522);
    feat_epic_devastating_critical_kama = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_KAMA", 523);
    feat_epic_devastating_critical_kukri = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_KUKRI", 524);
    feat_epic_devastating_critical_shuriken = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SHURIKEN", 525);
    feat_epic_devastating_critical_scythe = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_SCYTHE", 526);
    feat_epic_devastating_critical_katana = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_KATANA", 527);
    feat_epic_devastating_critical_bastardsword = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_BASTARDSWORD", 528);
    feat_epic_devastating_critical_diremace = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_DIREMACE", 529);
    feat_epic_devastating_critical_doubleaxe = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_DOUBLEAXE", 530);
    feat_epic_devastating_critical_twobladedsword = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_TWOBLADEDSWORD", 531);
    feat_epic_devastating_critical_creature = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_CREATURE", 532);
    feat_epic_energy_resistance_cold_1 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_1", 533);
    feat_epic_energy_resistance_cold_2 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_2", 534);
    feat_epic_energy_resistance_cold_3 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_3", 535);
    feat_epic_energy_resistance_cold_4 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_4", 536);
    feat_epic_energy_resistance_cold_5 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_5", 537);
    feat_epic_energy_resistance_cold_6 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_6", 538);
    feat_epic_energy_resistance_cold_7 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_7", 539);
    feat_epic_energy_resistance_cold_8 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_8", 540);
    feat_epic_energy_resistance_cold_9 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_9", 541);
    feat_epic_energy_resistance_cold_10 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_COLD_10", 542);
    feat_epic_energy_resistance_acid_1 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_1", 543);
    feat_epic_energy_resistance_acid_2 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_2", 544);
    feat_epic_energy_resistance_acid_3 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_3", 545);
    feat_epic_energy_resistance_acid_4 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_4", 546);
    feat_epic_energy_resistance_acid_5 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_5", 547);
    feat_epic_energy_resistance_acid_6 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_6", 548);
    feat_epic_energy_resistance_acid_7 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_7", 549);
    feat_epic_energy_resistance_acid_8 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_8", 550);
    feat_epic_energy_resistance_acid_9 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_9", 551);
    feat_epic_energy_resistance_acid_10 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ACID_10", 552);
    feat_epic_energy_resistance_fire_1 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_1", 553);
    feat_epic_energy_resistance_fire_2 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_2", 554);
    feat_epic_energy_resistance_fire_3 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_3", 555);
    feat_epic_energy_resistance_fire_4 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_4", 556);
    feat_epic_energy_resistance_fire_5 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_5", 557);
    feat_epic_energy_resistance_fire_6 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_6", 558);
    feat_epic_energy_resistance_fire_7 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_7", 559);
    feat_epic_energy_resistance_fire_8 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_8", 560);
    feat_epic_energy_resistance_fire_9 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_9", 561);
    feat_epic_energy_resistance_fire_10 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_FIRE_10", 562);
    feat_epic_energy_resistance_electrical_1 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_1", 563);
    feat_epic_energy_resistance_electrical_2 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_2", 564);
    feat_epic_energy_resistance_electrical_3 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_3", 565);
    feat_epic_energy_resistance_electrical_4 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_4", 566);
    feat_epic_energy_resistance_electrical_5 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_5", 567);
    feat_epic_energy_resistance_electrical_6 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_6", 568);
    feat_epic_energy_resistance_electrical_7 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_7", 569);
    feat_epic_energy_resistance_electrical_8 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_8", 570);
    feat_epic_energy_resistance_electrical_9 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_9", 571);
    feat_epic_energy_resistance_electrical_10 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_ELECTRICAL_10", 572);
    feat_epic_energy_resistance_sonic_1 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_1", 573);
    feat_epic_energy_resistance_sonic_2 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_2", 574);
    feat_epic_energy_resistance_sonic_3 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_3", 575);
    feat_epic_energy_resistance_sonic_4 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_4", 576);
    feat_epic_energy_resistance_sonic_5 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_5", 577);
    feat_epic_energy_resistance_sonic_6 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_6", 578);
    feat_epic_energy_resistance_sonic_7 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_7", 579);
    feat_epic_energy_resistance_sonic_8 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_8", 580);
    feat_epic_energy_resistance_sonic_9 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_9", 581);
    feat_epic_energy_resistance_sonic_10 = cr->add("FEAT_EPIC_ENERGY_RESISTANCE_SONIC_10", 582);

    feat_epic_fortitude = cr->add("FEAT_EPIC_FORTITUDE", 583);
    feat_epic_prowess = cr->add("FEAT_EPIC_PROWESS", 584);
    feat_epic_reflexes = cr->add("FEAT_EPIC_REFLEXES", 585);
    feat_epic_reputation = cr->add("FEAT_EPIC_REPUTATION", 586);
    feat_epic_skill_focus_animal_empathy = cr->add("FEAT_EPIC_SKILL_FOCUS_ANIMAL_EMPATHY", 587);
    feat_epic_skill_focus_appraise = cr->add("FEAT_EPIC_SKILL_FOCUS_APPRAISE", 588);
    feat_epic_skill_focus_concentration = cr->add("FEAT_EPIC_SKILL_FOCUS_CONCENTRATION", 589);
    feat_epic_skill_focus_craft_trap = cr->add("FEAT_EPIC_SKILL_FOCUS_CRAFT_TRAP", 590);
    feat_epic_skill_focus_disabletrap = cr->add("FEAT_EPIC_SKILL_FOCUS_DISABLETRAP", 591);
    feat_epic_skill_focus_discipline = cr->add("FEAT_EPIC_SKILL_FOCUS_DISCIPLINE", 592);
    feat_epic_skill_focus_heal = cr->add("FEAT_EPIC_SKILL_FOCUS_HEAL", 593);
    feat_epic_skill_focus_hide = cr->add("FEAT_EPIC_SKILL_FOCUS_HIDE", 594);
    feat_epic_skill_focus_listen = cr->add("FEAT_EPIC_SKILL_FOCUS_LISTEN", 595);
    feat_epic_skill_focus_lore = cr->add("FEAT_EPIC_SKILL_FOCUS_LORE", 596);
    feat_epic_skill_focus_movesilently = cr->add("FEAT_EPIC_SKILL_FOCUS_MOVESILENTLY", 597);
    feat_epic_skill_focus_openlock = cr->add("FEAT_EPIC_SKILL_FOCUS_OPENLOCK", 598);
    feat_epic_skill_focus_parry = cr->add("FEAT_EPIC_SKILL_FOCUS_PARRY", 599);
    feat_epic_skill_focus_perform = cr->add("FEAT_EPIC_SKILL_FOCUS_PERFORM", 600);
    feat_epic_skill_focus_persuade = cr->add("FEAT_EPIC_SKILL_FOCUS_PERSUADE", 601);
    feat_epic_skill_focus_pickpocket = cr->add("FEAT_EPIC_SKILL_FOCUS_PICKPOCKET", 602);
    feat_epic_skill_focus_search = cr->add("FEAT_EPIC_SKILL_FOCUS_SEARCH", 603);
    feat_epic_skill_focus_settrap = cr->add("FEAT_EPIC_SKILL_FOCUS_SETTRAP", 604);
    feat_epic_skill_focus_spellcraft = cr->add("FEAT_EPIC_SKILL_FOCUS_SPELLCRAFT", 605);
    feat_epic_skill_focus_spot = cr->add("FEAT_EPIC_SKILL_FOCUS_SPOT", 606);
    feat_epic_skill_focus_taunt = cr->add("FEAT_EPIC_SKILL_FOCUS_TAUNT", 607);
    feat_epic_skill_focus_tumble = cr->add("FEAT_EPIC_SKILL_FOCUS_TUMBLE", 608);
    feat_epic_skill_focus_usemagicdevice = cr->add("FEAT_EPIC_SKILL_FOCUS_USEMAGICDEVICE", 609);
    feat_epic_spell_focus_abjuration = cr->add("FEAT_EPIC_SPELL_FOCUS_ABJURATION", 610);
    feat_epic_spell_focus_conjuration = cr->add("FEAT_EPIC_SPELL_FOCUS_CONJURATION", 611);
    feat_epic_spell_focus_divination = cr->add("FEAT_EPIC_SPELL_FOCUS_DIVINATION", 612);
    feat_epic_spell_focus_enchantment = cr->add("FEAT_EPIC_SPELL_FOCUS_ENCHANTMENT", 613);
    feat_epic_spell_focus_evocation = cr->add("FEAT_EPIC_SPELL_FOCUS_EVOCATION", 614);
    feat_epic_spell_focus_illusion = cr->add("FEAT_EPIC_SPELL_FOCUS_ILLUSION", 615);
    feat_epic_spell_focus_necromancy = cr->add("FEAT_EPIC_SPELL_FOCUS_NECROMANCY", 616);
    feat_epic_spell_focus_transmutation = cr->add("FEAT_EPIC_SPELL_FOCUS_TRANSMUTATION", 617);
    feat_epic_spell_penetration = cr->add("FEAT_EPIC_SPELL_PENETRATION", 618);
    feat_epic_weapon_focus_club = cr->add("FEAT_EPIC_WEAPON_FOCUS_CLUB", 619);
    feat_epic_weapon_focus_dagger = cr->add("FEAT_EPIC_WEAPON_FOCUS_DAGGER", 620);
    feat_epic_weapon_focus_dart = cr->add("FEAT_EPIC_WEAPON_FOCUS_DART", 621);
    feat_epic_weapon_focus_heavycrossbow = cr->add("FEAT_EPIC_WEAPON_FOCUS_HEAVYCROSSBOW", 622);
    feat_epic_weapon_focus_lightcrossbow = cr->add("FEAT_EPIC_WEAPON_FOCUS_LIGHTCROSSBOW", 623);
    feat_epic_weapon_focus_lightmace = cr->add("FEAT_EPIC_WEAPON_FOCUS_LIGHTMACE", 624);
    feat_epic_weapon_focus_morningstar = cr->add("FEAT_EPIC_WEAPON_FOCUS_MORNINGSTAR", 625);
    feat_epic_weapon_focus_quarterstaff = cr->add("FEAT_EPIC_WEAPON_FOCUS_QUARTERSTAFF", 626);
    feat_epic_weapon_focus_shortspear = cr->add("FEAT_EPIC_WEAPON_FOCUS_SHORTSPEAR", 627);
    feat_epic_weapon_focus_sickle = cr->add("FEAT_EPIC_WEAPON_FOCUS_SICKLE", 628);
    feat_epic_weapon_focus_sling = cr->add("FEAT_EPIC_WEAPON_FOCUS_SLING", 629);
    feat_epic_weapon_focus_unarmed = cr->add("FEAT_EPIC_WEAPON_FOCUS_UNARMED", 630);
    feat_epic_weapon_focus_longbow = cr->add("FEAT_EPIC_WEAPON_FOCUS_LONGBOW", 631);
    feat_epic_weapon_focus_shortbow = cr->add("FEAT_EPIC_WEAPON_FOCUS_SHORTBOW", 632);
    feat_epic_weapon_focus_shortsword = cr->add("FEAT_EPIC_WEAPON_FOCUS_SHORTSWORD", 633);
    feat_epic_weapon_focus_rapier = cr->add("FEAT_EPIC_WEAPON_FOCUS_RAPIER", 634);
    feat_epic_weapon_focus_scimitar = cr->add("FEAT_EPIC_WEAPON_FOCUS_SCIMITAR", 635);
    feat_epic_weapon_focus_longsword = cr->add("FEAT_EPIC_WEAPON_FOCUS_LONGSWORD", 636);
    feat_epic_weapon_focus_greatsword = cr->add("FEAT_EPIC_WEAPON_FOCUS_GREATSWORD", 637);
    feat_epic_weapon_focus_handaxe = cr->add("FEAT_EPIC_WEAPON_FOCUS_HANDAXE", 638);
    feat_epic_weapon_focus_throwingaxe = cr->add("FEAT_EPIC_WEAPON_FOCUS_THROWINGAXE", 639);
    feat_epic_weapon_focus_battleaxe = cr->add("FEAT_EPIC_WEAPON_FOCUS_BATTLEAXE", 640);
    feat_epic_weapon_focus_greataxe = cr->add("FEAT_EPIC_WEAPON_FOCUS_GREATAXE", 641);
    feat_epic_weapon_focus_halberd = cr->add("FEAT_EPIC_WEAPON_FOCUS_HALBERD", 642);
    feat_epic_weapon_focus_lighthammer = cr->add("FEAT_EPIC_WEAPON_FOCUS_LIGHTHAMMER", 643);
    feat_epic_weapon_focus_lightflail = cr->add("FEAT_EPIC_WEAPON_FOCUS_LIGHTFLAIL", 644);
    feat_epic_weapon_focus_warhammer = cr->add("FEAT_EPIC_WEAPON_FOCUS_WARHAMMER", 645);
    feat_epic_weapon_focus_heavyflail = cr->add("FEAT_EPIC_WEAPON_FOCUS_HEAVYFLAIL", 646);
    feat_epic_weapon_focus_kama = cr->add("FEAT_EPIC_WEAPON_FOCUS_KAMA", 647);
    feat_epic_weapon_focus_kukri = cr->add("FEAT_EPIC_WEAPON_FOCUS_KUKRI", 648);
    feat_epic_weapon_focus_shuriken = cr->add("FEAT_EPIC_WEAPON_FOCUS_SHURIKEN", 649);
    feat_epic_weapon_focus_scythe = cr->add("FEAT_EPIC_WEAPON_FOCUS_SCYTHE", 650);
    feat_epic_weapon_focus_katana = cr->add("FEAT_EPIC_WEAPON_FOCUS_KATANA", 651);
    feat_epic_weapon_focus_bastardsword = cr->add("FEAT_EPIC_WEAPON_FOCUS_BASTARDSWORD", 652);
    feat_epic_weapon_focus_diremace = cr->add("FEAT_EPIC_WEAPON_FOCUS_DIREMACE", 653);
    feat_epic_weapon_focus_doubleaxe = cr->add("FEAT_EPIC_WEAPON_FOCUS_DOUBLEAXE", 654);
    feat_epic_weapon_focus_twobladedsword = cr->add("FEAT_EPIC_WEAPON_FOCUS_TWOBLADEDSWORD", 655);
    feat_epic_weapon_focus_creature = cr->add("FEAT_EPIC_WEAPON_FOCUS_CREATURE", 656);
    feat_epic_weapon_specialization_club = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_CLUB", 657);
    feat_epic_weapon_specialization_dagger = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_DAGGER", 658);
    feat_epic_weapon_specialization_dart = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_DART", 659);
    feat_epic_weapon_specialization_heavycrossbow = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_HEAVYCROSSBOW", 660);
    feat_epic_weapon_specialization_lightcrossbow = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTCROSSBOW", 661);
    feat_epic_weapon_specialization_lightmace = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTMACE", 662);
    feat_epic_weapon_specialization_morningstar = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_MORNINGSTAR", 663);
    feat_epic_weapon_specialization_quarterstaff = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_QUARTERSTAFF", 664);
    feat_epic_weapon_specialization_shortspear = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SHORTSPEAR", 665);
    feat_epic_weapon_specialization_sickle = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SICKLE", 666);
    feat_epic_weapon_specialization_sling = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SLING", 667);
    feat_epic_weapon_specialization_unarmed = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_UNARMED", 668);
    feat_epic_weapon_specialization_longbow = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_LONGBOW", 669);
    feat_epic_weapon_specialization_shortbow = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SHORTBOW", 670);
    feat_epic_weapon_specialization_shortsword = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SHORTSWORD", 671);
    feat_epic_weapon_specialization_rapier = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_RAPIER", 672);
    feat_epic_weapon_specialization_scimitar = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SCIMITAR", 673);
    feat_epic_weapon_specialization_longsword = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_LONGSWORD", 674);
    feat_epic_weapon_specialization_greatsword = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_GREATSWORD", 675);
    feat_epic_weapon_specialization_handaxe = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_HANDAXE", 676);
    feat_epic_weapon_specialization_throwingaxe = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_THROWINGAXE", 677);
    feat_epic_weapon_specialization_battleaxe = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_BATTLEAXE", 678);
    feat_epic_weapon_specialization_greataxe = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_GREATAXE", 679);
    feat_epic_weapon_specialization_halberd = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_HALBERD", 680);
    feat_epic_weapon_specialization_lighthammer = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTHAMMER", 681);
    feat_epic_weapon_specialization_lightflail = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_LIGHTFLAIL", 682);
    feat_epic_weapon_specialization_warhammer = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_WARHAMMER", 683);
    feat_epic_weapon_specialization_heavyflail = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_HEAVYFLAIL", 684);
    feat_epic_weapon_specialization_kama = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_KAMA", 685);
    feat_epic_weapon_specialization_kukri = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_KUKRI", 686);
    feat_epic_weapon_specialization_shuriken = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SHURIKEN", 687);
    feat_epic_weapon_specialization_scythe = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_SCYTHE", 688);
    feat_epic_weapon_specialization_katana = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_KATANA", 689);
    feat_epic_weapon_specialization_bastardsword = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_BASTARDSWORD", 690);
    feat_epic_weapon_specialization_diremace = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_DIREMACE", 691);
    feat_epic_weapon_specialization_doubleaxe = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_DOUBLEAXE", 692);
    feat_epic_weapon_specialization_twobladedsword = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_TWOBLADEDSWORD", 693);
    feat_epic_weapon_specialization_creature = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_CREATURE", 694);

    feat_epic_will = cr->add("FEAT_EPIC_WILL", 695);
    feat_epic_improved_combat_casting = cr->add("FEAT_EPIC_IMPROVED_COMBAT_CASTING", 696);
    feat_epic_improved_ki_strike_4 = cr->add("FEAT_EPIC_IMPROVED_KI_STRIKE_4", 697);
    feat_epic_improved_ki_strike_5 = cr->add("FEAT_EPIC_IMPROVED_KI_STRIKE_5", 698);
    feat_epic_improved_spell_resistance_1 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_1", 699);
    feat_epic_improved_spell_resistance_2 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_2", 700);
    feat_epic_improved_spell_resistance_3 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_3", 701);
    feat_epic_improved_spell_resistance_4 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_4", 702);
    feat_epic_improved_spell_resistance_5 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_5", 703);
    feat_epic_improved_spell_resistance_6 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_6", 704);
    feat_epic_improved_spell_resistance_7 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_7", 705);
    feat_epic_improved_spell_resistance_8 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_8", 706);
    feat_epic_improved_spell_resistance_9 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_9", 707);
    feat_epic_improved_spell_resistance_10 = cr->add("FEAT_EPIC_IMPROVED_SPELL_RESISTANCE_10", 708);
    feat_epic_overwhelming_critical_club = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_CLUB", 709);
    feat_epic_overwhelming_critical_dagger = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_DAGGER", 710);
    feat_epic_overwhelming_critical_dart = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_DART", 711);
    feat_epic_overwhelming_critical_heavycrossbow = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_HEAVYCROSSBOW", 712);
    feat_epic_overwhelming_critical_lightcrossbow = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTCROSSBOW", 713);
    feat_epic_overwhelming_critical_lightmace = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTMACE", 714);
    feat_epic_overwhelming_critical_morningstar = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_MORNINGSTAR", 715);
    feat_epic_overwhelming_critical_quarterstaff = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_QUARTERSTAFF", 716);
    feat_epic_overwhelming_critical_shortspear = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SHORTSPEAR", 717);
    feat_epic_overwhelming_critical_sickle = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SICKLE", 718);
    feat_epic_overwhelming_critical_sling = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SLING", 719);
    feat_epic_overwhelming_critical_unarmed = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_UNARMED", 720);
    feat_epic_overwhelming_critical_longbow = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_LONGBOW", 721);
    feat_epic_overwhelming_critical_shortbow = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SHORTBOW", 722);
    feat_epic_overwhelming_critical_shortsword = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SHORTSWORD", 723);
    feat_epic_overwhelming_critical_rapier = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_RAPIER", 724);
    feat_epic_overwhelming_critical_scimitar = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SCIMITAR", 725);
    feat_epic_overwhelming_critical_longsword = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_LONGSWORD", 726);
    feat_epic_overwhelming_critical_greatsword = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_GREATSWORD", 727);
    feat_epic_overwhelming_critical_handaxe = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_HANDAXE", 728);
    feat_epic_overwhelming_critical_throwingaxe = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_THROWINGAXE", 729);
    feat_epic_overwhelming_critical_battleaxe = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_BATTLEAXE", 730);
    feat_epic_overwhelming_critical_greataxe = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_GREATAXE", 731);
    feat_epic_overwhelming_critical_halberd = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_HALBERD", 732);
    feat_epic_overwhelming_critical_lighthammer = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTHAMMER", 733);
    feat_epic_overwhelming_critical_lightflail = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_LIGHTFLAIL", 734);
    feat_epic_overwhelming_critical_warhammer = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_WARHAMMER", 735);
    feat_epic_overwhelming_critical_heavyflail = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_HEAVYFLAIL", 736);
    feat_epic_overwhelming_critical_kama = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_KAMA", 737);
    feat_epic_overwhelming_critical_kukri = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_KUKRI", 738);
    feat_epic_overwhelming_critical_shuriken = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SHURIKEN", 739);
    feat_epic_overwhelming_critical_scythe = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_SCYTHE", 740);
    feat_epic_overwhelming_critical_katana = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_KATANA", 741);
    feat_epic_overwhelming_critical_bastardsword = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_BASTARDSWORD", 742);
    feat_epic_overwhelming_critical_diremace = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_DIREMACE", 743);
    feat_epic_overwhelming_critical_doubleaxe = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_DOUBLEAXE", 744);
    feat_epic_overwhelming_critical_twobladedsword = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_TWOBLADEDSWORD", 745);
    feat_epic_overwhelming_critical_creature = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_CREATURE", 746);
    feat_epic_perfect_health = cr->add("FEAT_EPIC_PERFECT_HEALTH", 747);
    feat_epic_self_concealment_10 = cr->add("FEAT_EPIC_SELF_CONCEALMENT_10", 748);
    feat_epic_self_concealment_20 = cr->add("FEAT_EPIC_SELF_CONCEALMENT_20", 749);
    feat_epic_self_concealment_30 = cr->add("FEAT_EPIC_SELF_CONCEALMENT_30", 750);
    feat_epic_self_concealment_40 = cr->add("FEAT_EPIC_SELF_CONCEALMENT_40", 751);
    feat_epic_self_concealment_50 = cr->add("FEAT_EPIC_SELF_CONCEALMENT_50", 752);
    feat_epic_superior_initiative = cr->add("FEAT_EPIC_SUPERIOR_INITIATIVE", 753);
    feat_epic_toughness_1 = cr->add("FEAT_EPIC_TOUGHNESS_1", 754);
    feat_epic_toughness_2 = cr->add("FEAT_EPIC_TOUGHNESS_2", 755);
    feat_epic_toughness_3 = cr->add("FEAT_EPIC_TOUGHNESS_3", 756);
    feat_epic_toughness_4 = cr->add("FEAT_EPIC_TOUGHNESS_4", 757);
    feat_epic_toughness_5 = cr->add("FEAT_EPIC_TOUGHNESS_5", 758);
    feat_epic_toughness_6 = cr->add("FEAT_EPIC_TOUGHNESS_6", 759);
    feat_epic_toughness_7 = cr->add("FEAT_EPIC_TOUGHNESS_7", 760);
    feat_epic_toughness_8 = cr->add("FEAT_EPIC_TOUGHNESS_8", 761);
    feat_epic_toughness_9 = cr->add("FEAT_EPIC_TOUGHNESS_9", 762);
    feat_epic_toughness_10 = cr->add("FEAT_EPIC_TOUGHNESS_10", 763);
    feat_epic_great_charisma_1 = cr->add("FEAT_EPIC_GREAT_CHARISMA_1", 764);
    feat_epic_great_charisma_2 = cr->add("FEAT_EPIC_GREAT_CHARISMA_2", 765);
    feat_epic_great_charisma_3 = cr->add("FEAT_EPIC_GREAT_CHARISMA_3", 766);
    feat_epic_great_charisma_4 = cr->add("FEAT_EPIC_GREAT_CHARISMA_4", 767);
    feat_epic_great_charisma_5 = cr->add("FEAT_EPIC_GREAT_CHARISMA_5", 768);
    feat_epic_great_charisma_6 = cr->add("FEAT_EPIC_GREAT_CHARISMA_6", 769);
    feat_epic_great_charisma_7 = cr->add("FEAT_EPIC_GREAT_CHARISMA_7", 770);
    feat_epic_great_charisma_8 = cr->add("FEAT_EPIC_GREAT_CHARISMA_8", 771);
    feat_epic_great_charisma_9 = cr->add("FEAT_EPIC_GREAT_CHARISMA_9", 772);
    feat_epic_great_charisma_10 = cr->add("FEAT_EPIC_GREAT_CHARISMA_10", 773);
    feat_epic_great_constitution_1 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_1", 774);
    feat_epic_great_constitution_2 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_2", 775);
    feat_epic_great_constitution_3 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_3", 776);
    feat_epic_great_constitution_4 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_4", 777);
    feat_epic_great_constitution_5 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_5", 778);
    feat_epic_great_constitution_6 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_6", 779);
    feat_epic_great_constitution_7 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_7", 780);
    feat_epic_great_constitution_8 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_8", 781);
    feat_epic_great_constitution_9 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_9", 782);
    feat_epic_great_constitution_10 = cr->add("FEAT_EPIC_GREAT_CONSTITUTION_10", 783);
    feat_epic_great_dexterity_1 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_1", 784);
    feat_epic_great_dexterity_2 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_2", 785);
    feat_epic_great_dexterity_3 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_3", 786);
    feat_epic_great_dexterity_4 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_4", 787);
    feat_epic_great_dexterity_5 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_5", 788);
    feat_epic_great_dexterity_6 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_6", 789);
    feat_epic_great_dexterity_7 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_7", 790);
    feat_epic_great_dexterity_8 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_8", 791);
    feat_epic_great_dexterity_9 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_9", 792);
    feat_epic_great_dexterity_10 = cr->add("FEAT_EPIC_GREAT_DEXTERITY_10", 793);
    feat_epic_great_intelligence_1 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_1", 794);
    feat_epic_great_intelligence_2 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_2", 795);
    feat_epic_great_intelligence_3 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_3", 796);
    feat_epic_great_intelligence_4 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_4", 797);
    feat_epic_great_intelligence_5 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_5", 798);
    feat_epic_great_intelligence_6 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_6", 799);
    feat_epic_great_intelligence_7 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_7", 800);
    feat_epic_great_intelligence_8 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_8", 801);
    feat_epic_great_intelligence_9 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_9", 802);
    feat_epic_great_intelligence_10 = cr->add("FEAT_EPIC_GREAT_INTELLIGENCE_10", 803);
    feat_epic_great_wisdom_1 = cr->add("FEAT_EPIC_GREAT_WISDOM_1", 804);
    feat_epic_great_wisdom_2 = cr->add("FEAT_EPIC_GREAT_WISDOM_2", 805);
    feat_epic_great_wisdom_3 = cr->add("FEAT_EPIC_GREAT_WISDOM_3", 806);
    feat_epic_great_wisdom_4 = cr->add("FEAT_EPIC_GREAT_WISDOM_4", 807);
    feat_epic_great_wisdom_5 = cr->add("FEAT_EPIC_GREAT_WISDOM_5", 808);
    feat_epic_great_wisdom_6 = cr->add("FEAT_EPIC_GREAT_WISDOM_6", 809);
    feat_epic_great_wisdom_7 = cr->add("FEAT_EPIC_GREAT_WISDOM_7", 810);
    feat_epic_great_wisdom_8 = cr->add("FEAT_EPIC_GREAT_WISDOM_8", 811);
    feat_epic_great_wisdom_9 = cr->add("FEAT_EPIC_GREAT_WISDOM_9", 812);
    feat_epic_great_wisdom_10 = cr->add("FEAT_EPIC_GREAT_WISDOM_10", 813);
    feat_epic_great_strength_1 = cr->add("FEAT_EPIC_GREAT_STRENGTH_1", 814);
    feat_epic_great_strength_2 = cr->add("FEAT_EPIC_GREAT_STRENGTH_2", 815);
    feat_epic_great_strength_3 = cr->add("FEAT_EPIC_GREAT_STRENGTH_3", 816);
    feat_epic_great_strength_4 = cr->add("FEAT_EPIC_GREAT_STRENGTH_4", 817);
    feat_epic_great_strength_5 = cr->add("FEAT_EPIC_GREAT_STRENGTH_5", 818);
    feat_epic_great_strength_6 = cr->add("FEAT_EPIC_GREAT_STRENGTH_6", 819);
    feat_epic_great_strength_7 = cr->add("FEAT_EPIC_GREAT_STRENGTH_7", 820);
    feat_epic_great_strength_8 = cr->add("FEAT_EPIC_GREAT_STRENGTH_8", 821);
    feat_epic_great_strength_9 = cr->add("FEAT_EPIC_GREAT_STRENGTH_9", 822);
    feat_epic_great_strength_10 = cr->add("FEAT_EPIC_GREAT_STRENGTH_10", 823);
    feat_epic_great_smiting_1 = cr->add("FEAT_EPIC_GREAT_SMITING_1", 824);
    feat_epic_great_smiting_2 = cr->add("FEAT_EPIC_GREAT_SMITING_2", 825);
    feat_epic_great_smiting_3 = cr->add("FEAT_EPIC_GREAT_SMITING_3", 826);
    feat_epic_great_smiting_4 = cr->add("FEAT_EPIC_GREAT_SMITING_4", 827);
    feat_epic_great_smiting_5 = cr->add("FEAT_EPIC_GREAT_SMITING_5", 828);
    feat_epic_great_smiting_6 = cr->add("FEAT_EPIC_GREAT_SMITING_6", 829);
    feat_epic_great_smiting_7 = cr->add("FEAT_EPIC_GREAT_SMITING_7", 830);
    feat_epic_great_smiting_8 = cr->add("FEAT_EPIC_GREAT_SMITING_8", 831);
    feat_epic_great_smiting_9 = cr->add("FEAT_EPIC_GREAT_SMITING_9", 832);
    feat_epic_great_smiting_10 = cr->add("FEAT_EPIC_GREAT_SMITING_10", 833);
    feat_epic_improved_sneak_attack_1 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_1", 834);
    feat_epic_improved_sneak_attack_2 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_2", 835);
    feat_epic_improved_sneak_attack_3 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_3", 836);
    feat_epic_improved_sneak_attack_4 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_4", 837);
    feat_epic_improved_sneak_attack_5 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_5", 838);
    feat_epic_improved_sneak_attack_6 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_6", 839);
    feat_epic_improved_sneak_attack_7 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_7", 840);
    feat_epic_improved_sneak_attack_8 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_8", 841);
    feat_epic_improved_sneak_attack_9 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_9", 842);
    feat_epic_improved_sneak_attack_10 = cr->add("FEAT_EPIC_IMPROVED_SNEAK_ATTACK_10", 843);
    feat_epic_improved_stunning_fist_1 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_1", 844);
    feat_epic_improved_stunning_fist_2 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_2", 845);
    feat_epic_improved_stunning_fist_3 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_3", 846);
    feat_epic_improved_stunning_fist_4 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_4", 847);
    feat_epic_improved_stunning_fist_5 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_5", 848);
    feat_epic_improved_stunning_fist_6 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_6", 849);
    feat_epic_improved_stunning_fist_7 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_7", 850);
    feat_epic_improved_stunning_fist_8 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_8", 851);
    feat_epic_improved_stunning_fist_9 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_9", 852);
    feat_epic_improved_stunning_fist_10 = cr->add("FEAT_EPIC_IMPROVED_STUNNING_FIST_10", 853);

    // feat_epic_planar_turning = cr->add( "FEAT_EPIC_PLANAR_TURNING"     ,  854);
    feat_epic_bane_of_enemies = cr->add("FEAT_EPIC_BANE_OF_ENEMIES", 855);
    feat_epic_dodge = cr->add("FEAT_EPIC_DODGE", 856);
    feat_epic_automatic_quicken_1 = cr->add("FEAT_EPIC_AUTOMATIC_QUICKEN_1", 857);
    feat_epic_automatic_quicken_2 = cr->add("FEAT_EPIC_AUTOMATIC_QUICKEN_2", 858);
    feat_epic_automatic_quicken_3 = cr->add("FEAT_EPIC_AUTOMATIC_QUICKEN_3", 859);
    feat_epic_automatic_silent_spell_1 = cr->add("FEAT_EPIC_AUTOMATIC_SILENT_SPELL_1", 860);
    feat_epic_automatic_silent_spell_2 = cr->add("FEAT_EPIC_AUTOMATIC_SILENT_SPELL_2", 861);
    feat_epic_automatic_silent_spell_3 = cr->add("FEAT_EPIC_AUTOMATIC_SILENT_SPELL_3", 862);
    feat_epic_automatic_still_spell_1 = cr->add("FEAT_EPIC_AUTOMATIC_STILL_SPELL_1", 863);
    feat_epic_automatic_still_spell_2 = cr->add("FEAT_EPIC_AUTOMATIC_STILL_SPELL_2", 864);
    feat_epic_automatic_still_spell_3 = cr->add("FEAT_EPIC_AUTOMATIC_STILL_SPELL_3", 865);

    feat_shou_disciple_martial_flurry_light = cr->add("FEAT_SHOU_DISCIPLE_MARTIAL_FLURRY_LIGHT", 866);
    feat_whirlwind_attack = cr->add("FEAT_WHIRLWIND_ATTACK", 867);
    feat_improved_whirlwind = cr->add("FEAT_IMPROVED_WHIRLWIND", 868);
    feat_mighty_rage = cr->add("FEAT_MIGHTY_RAGE", 869);
    feat_epic_lasting_inspiration = cr->add("FEAT_EPIC_LASTING_INSPIRATION", 870);
    feat_curse_song = cr->add("FEAT_CURSE_SONG", 871);
    feat_epic_wild_shape_undead = cr->add("FEAT_EPIC_WILD_SHAPE_UNDEAD", 872);
    feat_epic_wild_shape_dragon = cr->add("FEAT_EPIC_WILD_SHAPE_DRAGON", 873);
    feat_epic_spell_mummy_dust = cr->add("FEAT_EPIC_SPELL_MUMMY_DUST", 874);
    feat_epic_spell_dragon_knight = cr->add("FEAT_EPIC_SPELL_DRAGON_KNIGHT", 875);
    feat_epic_spell_hellball = cr->add("FEAT_EPIC_SPELL_HELLBALL", 876);
    feat_epic_spell_mage_armour = cr->add("FEAT_EPIC_SPELL_MAGE_ARMOUR", 877);
    feat_epic_spell_ruin = cr->add("FEAT_EPIC_SPELL_RUIN", 878);
    feat_weapon_of_choice_sickle = cr->add("FEAT_WEAPON_OF_CHOICE_SICKLE", 879);
    feat_weapon_of_choice_kama = cr->add("FEAT_WEAPON_OF_CHOICE_KAMA", 880);
    feat_weapon_of_choice_kukri = cr->add("FEAT_WEAPON_OF_CHOICE_KUKRI", 881);
    feat_ki_damage = cr->add("FEAT_KI_DAMAGE", 882);
    feat_increase_multiplier = cr->add("FEAT_INCREASE_MULTIPLIER", 883);
    feat_superior_weapon_focus = cr->add("FEAT_SUPERIOR_WEAPON_FOCUS", 884);
    feat_ki_critical = cr->add("FEAT_KI_CRITICAL", 885);
    feat_bone_skin_2 = cr->add("FEAT_BONE_SKIN_2", 886);
    feat_bone_skin_4 = cr->add("FEAT_BONE_SKIN_4", 887);
    feat_bone_skin_6 = cr->add("FEAT_BONE_SKIN_6", 888);
    feat_animate_dead = cr->add("FEAT_ANIMATE_DEAD", 889);
    feat_summon_undead = cr->add("FEAT_SUMMON_UNDEAD", 890);
    feat_deathless_vigor = cr->add("FEAT_DEATHLESS_VIGOR", 891);
    feat_undead_graft_1 = cr->add("FEAT_UNDEAD_GRAFT_1", 892);
    feat_undead_graft_2 = cr->add("FEAT_UNDEAD_GRAFT_2", 893);
    feat_tough_as_bone = cr->add("FEAT_TOUGH_AS_BONE", 894);
    feat_summon_greater_undead = cr->add("FEAT_SUMMON_GREATER_UNDEAD", 895);
    feat_deathless_mastery = cr->add("FEAT_DEATHLESS_MASTERY", 896);
    feat_deathless_master_touch = cr->add("FEAT_DEATHLESS_MASTER_TOUCH", 897);
    feat_greater_wildshape_1 = cr->add("FEAT_GREATER_WILDSHAPE_1", 898);
    feat_shou_disciple_martial_flurry_any = cr->add("FEAT_SHOU_DISCIPLE_MARTIAL_FLURRY_ANY", 899);
    feat_greater_wildshape_2 = cr->add("FEAT_GREATER_WILDSHAPE_2", 900);
    feat_greater_wildshape_3 = cr->add("FEAT_GREATER_WILDSHAPE_3", 901);
    feat_humanoid_shape = cr->add("FEAT_HUMANOID_SHAPE", 902);
    feat_greater_wildshape_4 = cr->add("FEAT_GREATER_WILDSHAPE_4", 903);
    feat_sacred_defense_1 = cr->add("FEAT_SACRED_DEFENSE_1", 904);
    feat_sacred_defense_2 = cr->add("FEAT_SACRED_DEFENSE_2", 905);
    feat_sacred_defense_3 = cr->add("FEAT_SACRED_DEFENSE_3", 906);
    feat_sacred_defense_4 = cr->add("FEAT_SACRED_DEFENSE_4", 907);
    feat_sacred_defense_5 = cr->add("FEAT_SACRED_DEFENSE_5", 908);
    feat_divine_wrath = cr->add("FEAT_DIVINE_WRATH", 909);
    feat_extra_smiting = cr->add("FEAT_EXTRA_SMITING", 910);
    feat_skill_focus_craft_armor = cr->add("FEAT_SKILL_FOCUS_CRAFT_ARMOR", 911);
    feat_skill_focus_craft_weapon = cr->add("FEAT_SKILL_FOCUS_CRAFT_WEAPON", 912);
    feat_epic_skill_focus_craft_armor = cr->add("FEAT_EPIC_SKILL_FOCUS_CRAFT_ARMOR", 913);
    feat_epic_skill_focus_craft_weapon = cr->add("FEAT_EPIC_SKILL_FOCUS_CRAFT_WEAPON", 914);
    feat_skill_focus_bluff = cr->add("FEAT_SKILL_FOCUS_BLUFF", 915);
    feat_skill_focus_intimidate = cr->add("FEAT_SKILL_FOCUS_INTIMIDATE", 916);
    feat_epic_skill_focus_bluff = cr->add("FEAT_EPIC_SKILL_FOCUS_BLUFF", 917);
    feat_epic_skill_focus_intimidate = cr->add("FEAT_EPIC_SKILL_FOCUS_INTIMIDATE", 918);

    feat_weapon_of_choice_club = cr->add("FEAT_WEAPON_OF_CHOICE_CLUB", 919);
    feat_weapon_of_choice_dagger = cr->add("FEAT_WEAPON_OF_CHOICE_DAGGER", 920);
    feat_weapon_of_choice_lightmace = cr->add("FEAT_WEAPON_OF_CHOICE_LIGHTMACE", 921);
    feat_weapon_of_choice_morningstar = cr->add("FEAT_WEAPON_OF_CHOICE_MORNINGSTAR", 922);
    feat_weapon_of_choice_quarterstaff = cr->add("FEAT_WEAPON_OF_CHOICE_QUARTERSTAFF", 923);
    feat_weapon_of_choice_shortspear = cr->add("FEAT_WEAPON_OF_CHOICE_SHORTSPEAR", 924);
    feat_weapon_of_choice_shortsword = cr->add("FEAT_WEAPON_OF_CHOICE_SHORTSWORD", 925);
    feat_weapon_of_choice_rapier = cr->add("FEAT_WEAPON_OF_CHOICE_RAPIER", 926);
    feat_weapon_of_choice_scimitar = cr->add("FEAT_WEAPON_OF_CHOICE_SCIMITAR", 927);
    feat_weapon_of_choice_longsword = cr->add("FEAT_WEAPON_OF_CHOICE_LONGSWORD", 928);
    feat_weapon_of_choice_greatsword = cr->add("FEAT_WEAPON_OF_CHOICE_GREATSWORD", 929);
    feat_weapon_of_choice_handaxe = cr->add("FEAT_WEAPON_OF_CHOICE_HANDAXE", 930);
    feat_weapon_of_choice_battleaxe = cr->add("FEAT_WEAPON_OF_CHOICE_BATTLEAXE", 931);
    feat_weapon_of_choice_greataxe = cr->add("FEAT_WEAPON_OF_CHOICE_GREATAXE", 932);
    feat_weapon_of_choice_halberd = cr->add("FEAT_WEAPON_OF_CHOICE_HALBERD", 933);
    feat_weapon_of_choice_lighthammer = cr->add("FEAT_WEAPON_OF_CHOICE_LIGHTHAMMER", 934);
    feat_weapon_of_choice_lightflail = cr->add("FEAT_WEAPON_OF_CHOICE_LIGHTFLAIL", 935);
    feat_weapon_of_choice_warhammer = cr->add("FEAT_WEAPON_OF_CHOICE_WARHAMMER", 936);
    feat_weapon_of_choice_heavyflail = cr->add("FEAT_WEAPON_OF_CHOICE_HEAVYFLAIL", 937);
    feat_weapon_of_choice_scythe = cr->add("FEAT_WEAPON_OF_CHOICE_SCYTHE", 938);
    feat_weapon_of_choice_katana = cr->add("FEAT_WEAPON_OF_CHOICE_KATANA", 939);
    feat_weapon_of_choice_bastardsword = cr->add("FEAT_WEAPON_OF_CHOICE_BASTARDSWORD", 940);
    feat_weapon_of_choice_diremace = cr->add("FEAT_WEAPON_OF_CHOICE_DIREMACE", 941);
    feat_weapon_of_choice_doubleaxe = cr->add("FEAT_WEAPON_OF_CHOICE_DOUBLEAXE", 942);
    feat_weapon_of_choice_twobladedsword = cr->add("FEAT_WEAPON_OF_CHOICE_TWOBLADEDSWORD", 943);

    feat_brew_potion = cr->add("FEAT_BREW_POTION", 944);
    feat_scribe_scroll = cr->add("FEAT_SCRIBE_SCROLL", 945);
    feat_craft_wand = cr->add("FEAT_CRAFT_WAND", 946);

    feat_dwarven_defender_defensive_stance = cr->add("FEAT_DWARVEN_DEFENDER_DEFENSIVE_STANCE", 947);
    feat_damage_reduction_6 = cr->add("FEAT_DAMAGE_REDUCTION_6", 948);
    feat_prestige_defensive_awareness_1 = cr->add("FEAT_PRESTIGE_DEFENSIVE_AWARENESS_1", 949);
    feat_prestige_defensive_awareness_2 = cr->add("FEAT_PRESTIGE_DEFENSIVE_AWARENESS_2", 950);
    feat_prestige_defensive_awareness_3 = cr->add("FEAT_PRESTIGE_DEFENSIVE_AWARENESS_3", 951);
    feat_weapon_focus_dwaxe = cr->add("FEAT_WEAPON_FOCUS_DWAXE", 952);
    feat_weapon_specialization_dwaxe = cr->add("FEAT_WEAPON_SPECIALIZATION_DWAXE", 953);
    feat_improved_critical_dwaxe = cr->add("FEAT_IMPROVED_CRITICAL_DWAXE", 954);
    feat_epic_devastating_critical_dwaxe = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_DWAXE", 955);
    feat_epic_weapon_focus_dwaxe = cr->add("FEAT_EPIC_WEAPON_FOCUS_DWAXE", 956);
    feat_epic_weapon_specialization_dwaxe = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_DWAXE", 957);
    feat_epic_overwhelming_critical_dwaxe = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_DWAXE", 958);
    feat_weapon_of_choice_dwaxe = cr->add("FEAT_WEAPON_OF_CHOICE_DWAXE", 959);
    feat_use_poison = cr->add("FEAT_USE_POISON", 960);

    feat_dragon_armor = cr->add("FEAT_DRAGON_ARMOR", 961);
    feat_dragon_abilities = cr->add("FEAT_DRAGON_ABILITIES", 962);
    feat_dragon_immune_paralysis = cr->add("FEAT_DRAGON_IMMUNE_PARALYSIS", 963);
    feat_dragon_immune_fire = cr->add("FEAT_DRAGON_IMMUNE_FIRE", 964);
    feat_dragon_dis_breath = cr->add("FEAT_DRAGON_DIS_BREATH", 965);
    feat_epic_fighter = cr->add("FEAT_EPIC_FIGHTER", 966);
    feat_epic_barbarian = cr->add("FEAT_EPIC_BARBARIAN", 967);
    feat_epic_bard = cr->add("FEAT_EPIC_BARD", 968);
    feat_epic_cleric = cr->add("FEAT_EPIC_CLERIC", 969);
    feat_epic_druid = cr->add("FEAT_EPIC_DRUID", 970);
    feat_epic_monk = cr->add("FEAT_EPIC_MONK", 971);
    feat_epic_paladin = cr->add("FEAT_EPIC_PALADIN", 972);
    feat_epic_ranger = cr->add("FEAT_EPIC_RANGER", 973);
    feat_epic_rogue = cr->add("FEAT_EPIC_ROGUE", 974);
    feat_epic_sorcerer = cr->add("FEAT_EPIC_SORCERER", 975);
    feat_epic_wizard = cr->add("FEAT_EPIC_WIZARD", 976);
    feat_epic_arcane_archer = cr->add("FEAT_EPIC_ARCANE_ARCHER", 977);
    feat_epic_assassin = cr->add("FEAT_EPIC_ASSASSIN", 978);
    feat_epic_blackguard = cr->add("FEAT_EPIC_BLACKGUARD", 979);
    feat_epic_shadowdancer = cr->add("FEAT_EPIC_SHADOWDANCER", 980);
    feat_epic_harper_scout = cr->add("FEAT_EPIC_HARPER_SCOUT", 981);
    feat_epic_divine_champion = cr->add("FEAT_EPIC_DIVINE_CHAMPION", 982);
    feat_epic_weapon_master = cr->add("FEAT_EPIC_WEAPON_MASTER", 983);
    feat_epic_pale_master = cr->add("FEAT_EPIC_PALE_MASTER", 984);
    feat_epic_dwarven_defender = cr->add("FEAT_EPIC_DWARVEN_DEFENDER", 985);
    feat_epic_shifter = cr->add("FEAT_EPIC_SHIFTER", 986);
    feat_epic_red_dragon_disc = cr->add("FEAT_EPIC_RED_DRAGON_DISC", 987);
    feat_epic_thundering_rage = cr->add("FEAT_EPIC_THUNDERING_RAGE", 988);
    feat_epic_terrifying_rage = cr->add("FEAT_EPIC_TERRIFYING_RAGE", 989);
    feat_epic_spell_epic_warding = cr->add("FEAT_EPIC_SPELL_EPIC_WARDING", 990);
    feat_weapon_focus_whip = cr->add("FEAT_WEAPON_FOCUS_WHIP", 993);
    feat_weapon_specialization_whip = cr->add("FEAT_WEAPON_SPECIALIZATION_WHIP", 994);
    feat_improved_critical_whip = cr->add("FEAT_IMPROVED_CRITICAL_WHIP", 995);
    feat_epic_devastating_critical_whip = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_WHIP", 996);
    feat_epic_weapon_focus_whip = cr->add("FEAT_EPIC_WEAPON_FOCUS_WHIP", 997);
    feat_epic_weapon_specialization_whip = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_WHIP", 998);
    feat_epic_overwhelming_critical_whip = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_WHIP", 999);
    feat_weapon_of_choice_whip = cr->add("FEAT_WEAPON_OF_CHOICE_WHIP", 1000);
    feat_epic_character = cr->add("FEAT_EPIC_CHARACTER", 1001);
    feat_epic_epic_shadowlord = cr->add("FEAT_EPIC_EPIC_SHADOWLORD", 1002);
    feat_epic_epic_fiend = cr->add("FEAT_EPIC_EPIC_FIEND", 1003);
    feat_prestige_death_attack_6 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_6", 1004);
    feat_prestige_death_attack_7 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_7", 1005);
    feat_prestige_death_attack_8 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_8", 1006);
    feat_blackguard_sneak_attack_4d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_4D6", 1007);
    feat_blackguard_sneak_attack_5d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_5D6", 1008);
    feat_blackguard_sneak_attack_6d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_6D6", 1009);
    feat_blackguard_sneak_attack_7d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_7D6", 1010);
    feat_blackguard_sneak_attack_8d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_8D6", 1011);
    feat_blackguard_sneak_attack_9d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_9D6", 1012);
    feat_blackguard_sneak_attack_10d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_10D6", 1013);
    feat_blackguard_sneak_attack_11d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_11D6", 1014);
    feat_blackguard_sneak_attack_12d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_12D6", 1015);
    feat_blackguard_sneak_attack_13d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_13D6", 1016);
    feat_blackguard_sneak_attack_14d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_14D6", 1017);
    feat_blackguard_sneak_attack_15d6 = cr->add("FEAT_BLACKGUARD_SNEAK_ATTACK_15D6", 1018);
    feat_prestige_death_attack_9 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_9", 1019);
    feat_prestige_death_attack_10 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_10", 1020);
    feat_prestige_death_attack_11 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_11", 1021);
    feat_prestige_death_attack_12 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_12", 1022);
    feat_prestige_death_attack_13 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_13", 1023);
    feat_prestige_death_attack_14 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_14", 1024);
    feat_prestige_death_attack_15 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_15", 1025);
    feat_prestige_death_attack_16 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_16", 1026);
    feat_prestige_death_attack_17 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_17", 1027);
    feat_prestige_death_attack_18 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_18", 1028);
    feat_prestige_death_attack_19 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_19", 1029);
    feat_prestige_death_attack_20 = cr->add("FEAT_PRESTIGE_DEATH_ATTACK_20", 1030);
    feat_shou_disciple_dodge_3 = cr->add("FEAT_SHOU_DISCIPLE_DODGE_3", 1031);
    feat_dragon_hdincrease_d6 = cr->add("FEAT_DRAGON_HDINCREASE_D6", 1042);
    feat_dragon_hdincrease_d8 = cr->add("FEAT_DRAGON_HDINCREASE_D8", 1043);
    feat_dragon_hdincrease_d10 = cr->add("FEAT_DRAGON_HDINCREASE_D10", 1044);
    feat_prestige_enchant_arrow_6 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_6", 1045);
    feat_prestige_enchant_arrow_7 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_7", 1046);
    feat_prestige_enchant_arrow_8 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_8", 1047);
    feat_prestige_enchant_arrow_9 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_9", 1048);
    feat_prestige_enchant_arrow_10 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_10", 1049);
    feat_prestige_enchant_arrow_11 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_11", 1050);
    feat_prestige_enchant_arrow_12 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_12", 1051);
    feat_prestige_enchant_arrow_13 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_13", 1052);
    feat_prestige_enchant_arrow_14 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_14", 1053);
    feat_prestige_enchant_arrow_15 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_15", 1054);
    feat_prestige_enchant_arrow_16 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_16", 1055);
    feat_prestige_enchant_arrow_17 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_17", 1056);
    feat_prestige_enchant_arrow_18 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_18", 1057);
    feat_prestige_enchant_arrow_19 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_19", 1058);
    feat_prestige_enchant_arrow_20 = cr->add("FEAT_PRESTIGE_ENCHANT_ARROW_20", 1059);
    feat_epic_outsider_shape = cr->add("FEAT_EPIC_OUTSIDER_SHAPE", 1060);
    feat_epic_construct_shape = cr->add("FEAT_EPIC_CONSTRUCT_SHAPE", 1061);
    feat_epic_shifter_infinite_wildshape_1 = cr->add("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_1", 1062);
    feat_epic_shifter_infinite_wildshape_2 = cr->add("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_2", 1063);
    feat_epic_shifter_infinite_wildshape_3 = cr->add("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_3", 1064);
    feat_epic_shifter_infinite_wildshape_4 = cr->add("FEAT_EPIC_SHIFTER_INFINITE_WILDSHAPE_4", 1065);
    feat_epic_shifter_infinite_humanoid_shape = cr->add("FEAT_EPIC_SHIFTER_INFINITE_HUMANOID_SHAPE", 1066);
    feat_epic_barbarian_damage_reduction = cr->add("FEAT_EPIC_BARBARIAN_DAMAGE_REDUCTION", 1067);
    feat_epic_druid_infinite_wildshape = cr->add("FEAT_EPIC_DRUID_INFINITE_WILDSHAPE", 1068);
    feat_epic_druid_infinite_elemental_shape = cr->add("FEAT_EPIC_DRUID_INFINITE_ELEMENTAL_SHAPE", 1069);
    feat_prestige_poison_save_epic = cr->add("FEAT_PRESTIGE_POISON_SAVE_EPIC", 1070);
    feat_epic_superior_weapon_focus = cr->add("FEAT_EPIC_SUPERIOR_WEAPON_FOCUS", 1071);

    feat_weapon_focus_trident = cr->add("FEAT_WEAPON_FOCUS_TRIDENT", 1072);
    feat_weapon_specialization_trident = cr->add("FEAT_WEAPON_SPECIALIZATION_TRIDENT", 1073);
    feat_improved_critical_trident = cr->add("FEAT_IMPROVED_CRITICAL_TRIDENT", 1074);
    feat_epic_devastating_critical_trident = cr->add("FEAT_EPIC_DEVASTATING_CRITICAL_TRIDENT", 1075);
    feat_epic_weapon_focus_trident = cr->add("FEAT_EPIC_WEAPON_FOCUS_TRIDENT", 1076);
    feat_epic_weapon_specialization_trident = cr->add("FEAT_EPIC_WEAPON_SPECIALIZATION_TRIDENT", 1077);
    feat_epic_overwhelming_critical_trident = cr->add("FEAT_EPIC_OVERWHELMING_CRITICAL_TRIDENT", 1078);
    feat_weapon_of_choice_trident = cr->add("FEAT_WEAPON_OF_CHOICE_TRIDENT", 1079);
    feat_pdk_rally = cr->add("FEAT_PDK_RALLY", 1080);
    feat_pdk_shield = cr->add("FEAT_PDK_SHIELD", 1081);
    feat_pdk_fear = cr->add("FEAT_PDK_FEAR", 1082);
    feat_pdk_wrath = cr->add("FEAT_PDK_WRATH", 1083);
    feat_pdk_stand = cr->add("FEAT_PDK_STAND", 1084);
    feat_pdk_inspire_1 = cr->add("FEAT_PDK_INSPIRE_1", 1085);
    feat_pdk_inspire_2 = cr->add("FEAT_PDK_INSPIRE_2", 1086);
    feat_mounted_combat = cr->add("FEAT_MOUNTED_COMBAT", 1087);
    feat_mounted_archery = cr->add("FEAT_MOUNTED_ARCHERY", 1088);
    feat_horse_menu = cr->add("FEAT_HORSE_MENU", 1089);
    feat_horse_mount = cr->add("FEAT_HORSE_MOUNT", 1090);
    feat_horse_dismount = cr->add("FEAT_HORSE_DISMOUNT", 1091);
    feat_horse_party_mount = cr->add("FEAT_HORSE_PARTY_MOUNT", 1092);
    feat_horse_party_dismount = cr->add("FEAT_HORSE_PARTY_DISMOUNT", 1093);
    feat_horse_assign_mount = cr->add("FEAT_HORSE_ASSIGN_MOUNT", 1094);
    feat_paladin_summon_mount = cr->add("FEAT_PALADIN_SUMMON_MOUNT", 1095);
    feat_player_tool_01 = cr->add("FEAT_PLAYER_TOOL_01", 1106);
    feat_player_tool_02 = cr->add("FEAT_PLAYER_TOOL_02", 1107);
    feat_player_tool_03 = cr->add("FEAT_PLAYER_TOOL_03", 1108);
    feat_player_tool_04 = cr->add("FEAT_PLAYER_TOOL_04", 1109);
    feat_player_tool_05 = cr->add("FEAT_PLAYER_TOOL_05", 1110);
    feat_player_tool_06 = cr->add("FEAT_PLAYER_TOOL_06", 1111);
    feat_player_tool_07 = cr->add("FEAT_PLAYER_TOOL_07", 1112);
    feat_player_tool_08 = cr->add("FEAT_PLAYER_TOOL_08", 1113);
    feat_player_tool_09 = cr->add("FEAT_PLAYER_TOOL_09", 1114);
    feat_player_tool_10 = cr->add("FEAT_PLAYER_TOOL_10", 1115);

    return true;
}

bool Profile::load_components() const
{
    nw::kernel::world().add<nw::AbilityArray>();
    nw::kernel::world().add<nw::BaseItemArray>();
    nw::kernel::world().add<nw::ClassArray>();
    nw::kernel::world().add<nw::FeatArray>();
    nw::kernel::world().add<nw::RaceArray>();
    nw::kernel::world().add<nw::SkillArray>();
    nw::kernel::world().add<nw::SpellArray>();
    return true;
}

/// @private
#define TDA_GET_UINT32(tda, name, row, column)      \
    do {                                            \
        if (tda.get_to(row, column, temp_int)) {    \
            name = static_cast<uint32_t>(temp_int); \
        }                                           \
    } while (0)

/// @private
#define TDA_GET_FLOAT(tda, name, row, column)                           \
    do {                                                                \
        if (tda.get_to(row, column, temp_float)) { name = temp_float; } \
    } while (0)

/// @private
#define TDA_GET_INT(tda, name, row, column)                         \
    do {                                                            \
        if (tda.get_to(row, column, temp_int)) { name = temp_int; } \
    } while (0)

/// @private
#define TDA_GET_BOOL(tda, name, row, column)                          \
    do {                                                              \
        if (tda.get_to(row, column, temp_int)) { name = !!temp_int; } \
    } while (0)

/// @private
#define TDA_GET_RES(tda, name, row, column, type)                                             \
    do {                                                                                      \
        if (tda.get_to(row, column, temp_string)) { name = nw::Resource{temp_string, type}; } \
    } while (0)

/// @private
#define TDA_GET_INDEX(tda, name, row, column)                                                                  \
    do {                                                                                                       \
        if (tda.get_to(row, column, temp_string)) { name = idxs->add(temp_string, static_cast<size_t>(row)); } \
    } while (0)

/// @private
#define TDA_GET_STRING(tda, name, row, column)                                       \
    do {                                                                             \
        if (tda.get_to(row, column, temp_string)) { name = std::move(temp_string); } \
    } while (0)

bool Profile::load_rules() const
{
    // == Set global rule functions ===========================================
    nw::kernel::rules().set_selector(selector);
    nw::kernel::rules().set_qualifier(match);

    // == Load Info ===========================================================

    auto* idxs = nw::kernel::world().get_mut<nw::IndexRegistry>();
    nw::TwoDA baseitems{nw::kernel::resman().demand({"baseitems"sv, nw::ResourceType::twoda})};
    nw::TwoDA classes{nw::kernel::resman().demand({"classes"sv, nw::ResourceType::twoda})};
    nw::TwoDA feat{nw::kernel::resman().demand({"feat"sv, nw::ResourceType::twoda})};
    nw::TwoDA racialtypes{nw::kernel::resman().demand({"racialtypes"sv, nw::ResourceType::twoda})};
    nw::TwoDA skills{nw::kernel::resman().demand({"skills"sv, nw::ResourceType::twoda})};
    nw::TwoDA spells{nw::kernel::resman().demand({"spells"sv, nw::ResourceType::twoda})};
    std::string temp_string;
    int temp_int = 0;
    float temp_float = 0.0f;

    // Abilities
    auto* ability_array = nw::kernel::world().get_mut<nw::AbilityArray>();
    ability_array->entries.reserve(6);
    ability_array->entries.push_back({135, ability_strength});
    ability_array->entries.push_back({133, ability_dexterity});
    ability_array->entries.push_back({132, ability_constitution});
    ability_array->entries.push_back({134, ability_intelligence});
    ability_array->entries.push_back({136, ability_wisdom});
    ability_array->entries.push_back({131, ability_charisma});

    // BaseItems
    auto* baseitem_array = nw::kernel::world().get_mut<nw::BaseItemArray>();
    if (baseitem_array && baseitems.is_valid()) {
        for (size_t i = 0; i < baseitems.rows(); ++i) {
            if (!baseitems.get_to(0, "label", temp_string)) { continue; }

            nw::BaseItem info;
            TDA_GET_UINT32(baseitems, info.name, i, "Name");
            TDA_GET_INT(baseitems, info.inventory_slot_size.first, i, "InvSlotWidth");
            TDA_GET_INT(baseitems, info.inventory_slot_size.second, i, "InvSlotHeight");
            TDA_GET_UINT32(baseitems, info.equipable_slots, i, "EquipableSlots");
            TDA_GET_BOOL(baseitems, info.can_rotate_icon, i, "CanRotateIcon");
            // ModelType
            // ItemClass
            TDA_GET_BOOL(baseitems, info.gender_specific, i, "GenderSpecific");
            TDA_GET_BOOL(baseitems, std::get<0>(info.composite_env_map), i, "Part1EnvMap");
            TDA_GET_BOOL(baseitems, std::get<1>(info.composite_env_map), i, "Part2EnvMap");
            TDA_GET_BOOL(baseitems, std::get<2>(info.composite_env_map), i, "Part3EnvMap");
            TDA_GET_RES(baseitems, info.default_model, i, "DefaultModel", nw::ResourceType::mdl);
            TDA_GET_STRING(baseitems, info.default_icon, i, "DefaultIcon");
            TDA_GET_BOOL(baseitems, info.is_container, i, "Container");
            // WeaponWield
            // WeaponType
            // WeaponSize
            TDA_GET_BOOL(baseitems, info.ranged, i, "RangedWeapon");
            // PrefAttackDist
            // MinRange
            // MaxRange
            // NumDice
            // DieToRoll
            TDA_GET_INT(baseitems, info.crit_threat, i, "CritThreat");
            TDA_GET_INT(baseitems, info.crit_multiplier, i, "CritHitMult");
            // Category
            // BaseCost
            // Stacking
            // ItemMultiplier
            TDA_GET_UINT32(baseitems, info.description, i, "Description");
            // InvSoundType
            // MaxProps
            // MinProps
            // PropColumn
            // StorePanel

            // AC_Enchant
            // BaseAC
            // ArmorCheckPen
            // BaseItemStatRef
            // ChargesStarting
            // RotateOnGround
            // TenthLBS
            // WeaponMatType
            // AmmunitionType
            // QBBehaviour
            // ArcaneSpellFailure
            // %AnimSlashL
            // %AnimSlashR
            // %AnimSlashS
            // StorePanelSort
            // ILRStackSize

            if (baseitems.get_to(i, "WeaponFocusFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            if (baseitems.get_to(i, "EpicWeaponFocusFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            if (baseitems.get_to(i, "WeaponSpecializationFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            if (baseitems.get_to(i, "EpicWeaponSpecializationFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            if (baseitems.get_to(i, "WeaponImprovedCriticalFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            if (baseitems.get_to(i, "EpicWeaponOverwhelmingCriticalFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            if (baseitems.get_to(i, "EpicWeaponDevastatingCriticalFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            if (baseitems.get_to(i, "WeaponOfChoiceFeat", temp_int)) {
                info.weapon_modifiers_feats.push_back(temp_int);
            }

            std::sort(std::begin(info.weapon_modifiers_feats),
                std::end(info.weapon_modifiers_feats));

            TDA_GET_BOOL(baseitems, info.is_monk_weapon, i, "IsMonkWeapon");
            // WeaponFinesseMinimumCreatureSize

            baseitem_array->entries.push_back(std::move(info));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'baseitems.2da'");
    }

    // Classes
    auto* class_array = nw::kernel::world().get_mut<nw::ClassArray>();
    if (class_array && classes.is_valid()) {
        class_array->entries.reserve(classes.rows());
        for (size_t i = 0; i < classes.rows(); ++i) {
            nw::Class info;

            if (classes.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(classes, info.name, i, "Name");
                TDA_GET_UINT32(classes, info.plural, i, "Plural");
                TDA_GET_UINT32(classes, info.lower, i, "Lower");
                TDA_GET_UINT32(classes, info.description, i, "Description");
                TDA_GET_RES(classes, info.icon, i, "Icon", nw::ResourceType::texture);
                TDA_GET_INT(classes, info.hitdie, i, "HitDie");
                TDA_GET_RES(classes, info.attack_bonus_table, i, "AttackBonusTable", nw::ResourceType::twoda);
                TDA_GET_RES(classes, info.feats_table, i, "FeatsTable", nw::ResourceType::twoda);
                TDA_GET_RES(classes, info.saving_throw_table, i, "SavingThrowTable", nw::ResourceType::twoda);
                TDA_GET_RES(classes, info.skills_table, i, "SkillsTable", nw::ResourceType::twoda);
                TDA_GET_RES(classes, info.bonus_feats_table, i, "BonusFeatsTable", nw::ResourceType::twoda);
                TDA_GET_INT(classes, info.skill_point_base, i, "SkillPointBase");
                TDA_GET_RES(classes, info.spell_gain_table, i, "SpellGainTable", nw::ResourceType::twoda);
                TDA_GET_RES(classes, info.spell_known_table, i, "SpellKnownTable", nw::ResourceType::twoda);
                TDA_GET_BOOL(classes, info.player_class, i, "PlayerClass");
                TDA_GET_BOOL(classes, info.spellcaster, i, "SpellCaster");

                TDA_GET_INT(classes, info.primary_ability, i, "PrimaryAbil");
                TDA_GET_UINT32(classes, info.alignment_restriction, i, "AlignRestrict");
                TDA_GET_UINT32(classes, info.alignment_rstrction_type, i, "AlignRstrctType");
                TDA_GET_BOOL(classes, info.invert_restriction, i, "InvertRestrict");
                TDA_GET_INDEX(classes, info.index, i, "Constant");
                TDA_GET_RES(classes, info.prereq_table, i, "PreReqTable", nw::ResourceType::twoda);
                TDA_GET_INT(classes, info.max_level, i, "MaxLevel");
                TDA_GET_INT(classes, info.xp_penalty, i, "XPPenalty");

                if (!info.spellcaster) {
                    TDA_GET_INT(classes, info.arcane_spellgain_mod, i, "ArcSpellLvlMod");
                    TDA_GET_INT(classes, info.divine_spellgain_mod, i, "DivSpellLvlMod");
                }

                TDA_GET_INT(classes, info.epic_level_limit, i, "EpicLevel");
                TDA_GET_INT(classes, info.package, i, "Package");
                TDA_GET_RES(classes, info.stat_gain_table, i, "StatGainTable", nw::ResourceType::twoda);

                // No point in checking for anyone else
                if (info.spellcaster) {
                    TDA_GET_BOOL(classes, info.memorizes_spells, i, "MemorizesSpells");
                    TDA_GET_BOOL(classes, info.spellbook_restricted, i, "SpellbookRestricted");
                    TDA_GET_BOOL(classes, info.pick_domains, i, "PickDomains");
                    TDA_GET_BOOL(classes, info.pick_school, i, "PickSchool");
                    TDA_GET_BOOL(classes, info.learn_scroll, i, "LearnScroll");
                    TDA_GET_BOOL(classes, info.arcane, i, "Arcane");
                    TDA_GET_BOOL(classes, info.arcane_spell_failure, i, "ASF");
                    TDA_GET_INT(classes, info.caster_ability, i, "SpellcastingAbil");
                    TDA_GET_STRING(classes, info.spell_table_column, i, "SpellTableColumn");
                    TDA_GET_FLOAT(classes, info.caster_level_multiplier, i, "CLMultiplier");
                    TDA_GET_INT(classes, info.level_min_caster, i, "MinCastingLevel");
                    TDA_GET_INT(classes, info.level_min_associate, i, "MinAssociateLevel");
                }

                if (info.spellcaster) {
                    TDA_GET_BOOL(classes, info.can_cast_spontaneously, i, "CanCastSpontaneously");
                }
            }
            class_array->entries.push_back(info);
        }

    } else {
        throw std::runtime_error("rules: failed to load 'classes.2da'");
    }

    // Feats
    auto* feat_array = nw::kernel::world().get_mut<nw::FeatArray>();
    if (feat_array && feat.is_valid()) {
        feat_array->entries.reserve(feat.rows());
        for (size_t i = 0; i < feat.rows(); ++i) {
            nw::Feat info;

            if (!feat.get_to(0, "label", temp_string)) { continue; }

            TDA_GET_UINT32(feat, info.name, i, "FEAT");
            TDA_GET_UINT32(feat, info.description, i, "DESCRIPTION");
            TDA_GET_RES(feat, info.icon, i, "ICON", nw::ResourceType::texture);
            TDA_GET_BOOL(feat, info.all_can_use, i, "ALLCLASSESCANUSE");
            TDA_GET_INT(feat, info.category, i, "CATEGORY");
            TDA_GET_INT(feat, info.max_cr, i, "MAXCR");
            TDA_GET_INT(feat, info.spell, i, "SPELLID");
            TDA_GET_UINT32(feat, info.successor, i, "SUCCESSOR");
            TDA_GET_FLOAT(feat, info.cr_value, i, "CRValue");
            TDA_GET_INT(feat, info.uses, i, "USESPERDAY");
            TDA_GET_INT(feat, info.master, i, "MASTERFEAT");
            TDA_GET_BOOL(feat, info.target_self, i, "TARGETSELF");
            TDA_GET_INDEX(feat, info.index, i, "Constant");
            TDA_GET_INT(feat, info.tools_categories, i, "TOOLSCATEGORIES");
            TDA_GET_BOOL(feat, info.hostile, i, "HostileFeat");
            TDA_GET_BOOL(feat, info.epic, i, "PreReqEpic");
            TDA_GET_BOOL(feat, info.requires_action, i, "ReqAction");

            feat_array->entries.push_back(std::move(info));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'feat.2da'");
    }

    // Races
    auto* race_array = nw::kernel::world().get_mut<nw::RaceArray>();
    if (race_array && racialtypes.is_valid()) {
        race_array->entries.reserve(racialtypes.rows());
        for (size_t i = 0; i < racialtypes.rows(); ++i) {
            nw::Race info;

            if (racialtypes.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(racialtypes, info.name, i, "Name");
                TDA_GET_UINT32(racialtypes, info.name_conversation, i, "ConverName");
                TDA_GET_UINT32(racialtypes, info.name_conversation_lower, i, "ConverNameLower");
                TDA_GET_UINT32(racialtypes, info.name_plural, i, "NamePlural");
                TDA_GET_UINT32(racialtypes, info.description, i, "Description");
                TDA_GET_RES(racialtypes, info.icon, i, "Icon", nw::ResourceType::texture);
                TDA_GET_INT(racialtypes, info.appearance, i, "appearance");
                TDA_GET_INT(racialtypes, info.ability_modifiers[0], i, "StrAdjust");
                TDA_GET_INT(racialtypes, info.ability_modifiers[1], i, "DexAdjust");
                TDA_GET_INT(racialtypes, info.ability_modifiers[2], i, "ConAdjust");
                TDA_GET_INT(racialtypes, info.ability_modifiers[3], i, "IntAdjust");
                TDA_GET_INT(racialtypes, info.ability_modifiers[4], i, "WisAdjust");
                TDA_GET_INT(racialtypes, info.ability_modifiers[5], i, "ChaAdjust");
                TDA_GET_INT(racialtypes, info.favored_class, i, "Favored");
                TDA_GET_RES(racialtypes, info.feats_table, i, "FeatsTable", nw::ResourceType::twoda);
                TDA_GET_UINT32(racialtypes, info.biography, i, "Biography");
                TDA_GET_BOOL(racialtypes, info.player_race, i, "PlayerRace");
                TDA_GET_INDEX(racialtypes, info.index, i, "Constant");
                TDA_GET_INT(racialtypes, info.age, i, "age");
                TDA_GET_FLOAT(racialtypes, info.cr_modifier, i, "CRModifier");
                TDA_GET_INT(racialtypes, info.feats_extra_1st_level, i, "ExtraFeatsAtFirstLevel");
                TDA_GET_INT(racialtypes, info.skillpoints_extra_per_level, i, "ExtraSkillPointsPerLevel");
                TDA_GET_INT(racialtypes, info.skillpoints_1st_level_multiplier, i, "FirstLevelSkillPointsMultiplier");
                TDA_GET_INT(racialtypes, info.ability_point_buy_number, i, "AbilitiesPointBuyNumber");
                TDA_GET_INT(racialtypes, info.feats_normal_level, i, "NormalFeatEveryNthLevel");
                TDA_GET_INT(racialtypes, info.feats_normal_amount, i, "NumberNormalFeatsEveryNthLevel");
                TDA_GET_INT(racialtypes, info.skillpoints_ability, i, "SkillPointModifierAbility");
            }

            race_array->entries.push_back(info);
        }
    } else {
        throw std::runtime_error("rules: failed to load 'racialtypes.2da'");
    }

    // Skills
    auto* skill_array = nw::kernel::world().get_mut<nw::SkillArray>();
    if (skill_array && skills.is_valid()) {
        skill_array->entries.reserve(skills.rows());

        for (size_t i = 0; i < skills.rows(); ++i) {
            nw::Skill info;

            if (skills.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(skills, info.name, i, "Name");
                TDA_GET_UINT32(skills, info.description, i, "Description");
                TDA_GET_RES(skills, info.icon, i, "Icon", nw::ResourceType::texture);
                TDA_GET_BOOL(skills, info.untrained, i, "Untrained");

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

                TDA_GET_BOOL(skills, info.armor_check_penalty, i, "ArmorCheckPenalty");
                TDA_GET_BOOL(skills, info.all_can_use, i, "AllClassesCanUse");
                TDA_GET_INDEX(skills, info.index, i, "Constant");
                TDA_GET_BOOL(skills, info.hostile, i, "HostileSkill");
            }

            skill_array->entries.push_back(info);
        }
    } else {
        throw std::runtime_error("rules: failed to load 'skills.2da'");
    }

    // Spells
    auto* spell_array = nw::kernel::world().get_mut<nw::SpellArray>();
    if (spell_array && spells.is_valid()) {
        spell_array->entries.reserve(spells.rows());
        for (size_t i = 0; i < spells.rows(); ++i) {
            nw::Spell info;
            // float temp_float = 0.0f;

            if (spells.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(spells, info.name, i, "Name");
                TDA_GET_RES(spells, info.icon, i, "IconResRef", nw::ResourceType::texture);
            }

            spell_array->entries.push_back(info);
        }
    } else {
        throw std::runtime_error("rules: failed to load 'spells.2da'");
    }

    // == Process Requirements ================================================

    // BaseItems
    for (size_t i = 0; i < baseitem_array->entries.size(); ++i) {
        nw::BaseItem& info = baseitem_array->entries[i];
        if (baseitems.get_to(i, "ReqFeat0", temp_int)) {
            info.feat_requirement.add(qual::feat(
                feat_array->entries[static_cast<size_t>(temp_int)].index));
        }

        if (baseitems.get_to(i, "ReqFeat1", temp_int)) {
            info.feat_requirement.add(qual::feat(
                feat_array->entries[static_cast<size_t>(temp_int)].index));
        }

        if (baseitems.get_to(i, "ReqFeat2", temp_int)) {
            info.feat_requirement.add(qual::feat(
                feat_array->entries[static_cast<size_t>(temp_int)].index));
        }

        if (baseitems.get_to(i, "ReqFeat3", temp_int)) {
            info.feat_requirement.add(qual::feat(
                feat_array->entries[static_cast<size_t>(temp_int)].index));
        }

        if (baseitems.get_to(i, "ReqFeat4", temp_int)) {
            info.feat_requirement.add(qual::feat(
                feat_array->entries[static_cast<size_t>(temp_int)].index));
        }
    }

    // Classes

    // Feats

    for (size_t i = 0; i < feat_array->entries.size(); ++i) {
        nw::Feat& info = feat_array->entries[i];
        if (!info.valid()) { continue; }

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
            info.requirements.add(qual::feat(
                feat_array->entries[static_cast<size_t>(temp_int)].index));
        }

        if (feat.get_to(i, "PREREQFEAT2", temp_int)) {
            info.requirements.add(qual::feat(
                feat_array->entries[static_cast<size_t>(temp_int)].index));
        }
    }

    // Races

    // == Load Modifiers ======================================================
    load_modifiers();

    return true;
}

#undef TDA_GET_STRING
#undef TDA_GET_STRREF
#undef TDA_GET_FLOAT
#undef TDA_GET_INT
#undef TDA_GET_BOOL
#undef TDA_GET_RES

} // namespace nwn1
