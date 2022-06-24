#pragma once

#include "../resources/Resource.hpp"
#include "system.hpp"

#include <cstdint>

namespace nw {

struct Creature;

struct Class {
    Requirement requirements;

    uint32_t name = 0xFFFFFFFF;
    uint32_t plural = 0xFFFFFFFF;
    uint32_t lower = 0xFFFFFFFF;
    uint32_t description = 0xFFFFFFFF;
    Resource icon;
    int hitdie = 0;
    Resource attack_bonus_table;
    Resource feats_table;
    Resource saving_throw_table;
    Resource skills_table;
    Resource bonus_feats_table;
    int skill_point_base = 0;
    Resource spell_gain_table;
    Resource spell_known_table;
    bool player_class = false;
    bool spellcaster = false;
    int primary_ability;
    uint32_t alignment_restriction = 0;
    uint32_t alignment_restriction_type = 0;
    bool invert_restriction = false;
    Index index;
    Resource prereq_table;
    int max_level = 0;
    int xp_penalty = 0;
    int arcane_spellgain_mod = 0;
    int divine_spellgain_mod = 0;
    int epic_level_limit = -1;
    int package = 0;
    Resource stat_gain_table;
    bool memorizes_spells = false;
    bool spellbook_restricted = false;
    bool pick_domains = false;
    bool pick_school = false;
    bool learn_scroll = false;
    bool arcane = false;
    bool arcane_spell_failure = false;
    int caster_ability;
    std::string spell_table_column;
    float caster_level_multiplier = 1.0f;
    int level_min_caster = 0;
    int level_min_associate = 0;
    bool can_cast_spontaneously = false;

    operator bool() const noexcept { return name != 0xFFFFFFFF; }
};

/// Class Singleton component
struct ClassArray {
    std::vector<Class> entries;
};

// Unimplemented
// - Str, Dex, Con, Wis, Int, Cha
// - EffCRLvl01-20

} // namespace nw
