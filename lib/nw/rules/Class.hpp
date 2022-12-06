#pragma once

#include "../objects/Saves.hpp"
#include "../resources/Resource.hpp"
#include "../util/InternedString.hpp"
#include "rule_type.hpp"
#include "system.hpp"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <limits>
#include <set>

namespace nw {

struct Creature;
struct TwoDARowView;

DECLARE_RULE_TYPE(Class);

struct ClassStatGain {
    std::array<int, 6> ability;
    int natural_ac = 0;
};

struct ClassRequirement {
    Requirement main{};
    Requirement feat_or{{}, false};
    Requirement class_or{{}, false};
    Requirement class_not{};
};

struct ClassInfo {
    ClassInfo() = default;
    ClassInfo(const TwoDARowView& tda);

    bool valid() const noexcept { return name != 0xFFFFFFFF; }

    ClassRequirement requirements;

    uint32_t name = 0xFFFFFFFF;
    uint32_t plural = 0xFFFFFFFF;
    uint32_t lower = 0xFFFFFFFF;
    uint32_t description = 0xFFFFFFFF;
    Resource icon;
    int hitdie = 0;
    const std::vector<int>* attack_bonus_table = nullptr;
    Resource feats_table;
    Resource saving_throw_table;
    std::vector<Saves> class_saves;
    Resource skill_table;
    std::vector<int> class_skills;
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
    InternedString constant;
    Resource prereq_table;
    int max_level = 0;
    int xp_penalty = 0;
    int arcane_spellgain_mod = 0;
    int divine_spellgain_mod = 0;
    int epic_level_limit = -1;
    int package = 0;
    Resource stat_gain_table;
    std::vector<ClassStatGain> class_stat_gain;
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
};

/// Class Singleton component
struct ClassArray {
    using map_type = absl::flat_hash_map<
        InternedString,
        Class,
        InternedStringHash,
        InternedStringEq>;

    const ClassInfo* get(Class class_) const noexcept;
    bool is_valid(Class class_) const noexcept;
    Class from_constant(std::string_view constant) const;

    /// Gets class base attack from attack tables
    int get_base_attack_bonus(Class class_, size_t level) const;

    /// Gets class save bonuses from save tables
    Saves get_class_save_bonus(Class class_, size_t level) const;

    /// Determines if skill is a class skill
    bool get_is_class_skill(Class class_, Skill skill) const;

    /// Gets class Natural AC gain
    int get_natural_ac(Class class_, size_t level) const;

    /// Gets class requirements
    const ClassRequirement* get_requirement(Class class_) const;

    /// Gets class ability gain
    int get_stat_gain(Class class_, Ability ability, size_t level) const;

    std::set<std::vector<int>> attack_tables;
    std::vector<int> stat_gain_tables;

    std::vector<ClassInfo> entries;
    map_type constant_to_index;
};

// Unimplemented
// - Str, Dex, Con, Wis, Int, Cha
// - EffCRLvl01-20

} // namespace nw
