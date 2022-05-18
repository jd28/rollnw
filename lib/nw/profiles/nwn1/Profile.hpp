#pragma once

#include "../../rules/Constant.hpp"
#include "../GameProfile.hpp"

namespace nwn1 {

extern nw::Constant ability_strength;
extern nw::Constant ability_dexterity;
extern nw::Constant ability_constitution;
extern nw::Constant ability_intelligence;
extern nw::Constant ability_wisdom;
extern nw::Constant ability_charisma;

extern nw::Constant racial_type_dwarf;
extern nw::Constant racial_type_elf;
extern nw::Constant racial_type_gnome;
extern nw::Constant racial_type_halfling;
extern nw::Constant racial_type_halfelf;
extern nw::Constant racial_type_halforc;
extern nw::Constant racial_type_human;
extern nw::Constant racial_type_aberration;
extern nw::Constant racial_type_animal;
extern nw::Constant racial_type_beast;
extern nw::Constant racial_type_construct;
extern nw::Constant racial_type_dragon;
extern nw::Constant racial_type_humanoid_goblinoid;
extern nw::Constant racial_type_humanoid_monstrous;
extern nw::Constant racial_type_humanoid_orc;
extern nw::Constant racial_type_humanoid_reptilian;
extern nw::Constant racial_type_elemental;
extern nw::Constant racial_type_fey;
extern nw::Constant racial_type_giant;
extern nw::Constant racial_type_magical_beast;
extern nw::Constant racial_type_outsider;
extern nw::Constant racial_type_shapechanger;
extern nw::Constant racial_type_undead;
extern nw::Constant racial_type_vermin;
extern nw::Constant racial_type_all;
extern nw::Constant racial_type_invalid;
extern nw::Constant racial_type_ooze;

extern nw::Constant skill_animal_empathy;
extern nw::Constant skill_concentration;
extern nw::Constant skill_disable_trap;
extern nw::Constant skill_discipline;
extern nw::Constant skill_heal;
extern nw::Constant skill_hide;
extern nw::Constant skill_listen;
extern nw::Constant skill_lore;
extern nw::Constant skill_move_silently;
extern nw::Constant skill_open_lock;
extern nw::Constant skill_parry;
extern nw::Constant skill_perform;
extern nw::Constant skill_persuade;
extern nw::Constant skill_pick_pocket;
extern nw::Constant skill_search;
extern nw::Constant skill_set_trap;
extern nw::Constant skill_spellcraft;
extern nw::Constant skill_spot;
extern nw::Constant skill_taunt;
extern nw::Constant skill_use_magic_device;
extern nw::Constant skill_appraise;
extern nw::Constant skill_tumble;
extern nw::Constant skill_craft_trap;
extern nw::Constant skill_bluff;
extern nw::Constant skill_intimidate;
extern nw::Constant skill_craft_armor;
extern nw::Constant skill_craft_weapon;
extern nw::Constant skill_ride;

struct Profile : nw::GameProfile {
    virtual ~Profile() = default;

    virtual bool load_constants() const override;
    virtual bool load_compontents() const override;
    virtual bool load_rules() const override;
};

} // namespace nwn1
