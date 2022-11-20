#include "Class.hpp"

#include "../formats/TwoDA.hpp"
#include "../kernel/Resources.hpp"
#include "../kernel/Rules.hpp"

namespace nw {

ClassInfo::ClassInfo(const TwoDARowView& tda)
{
    auto class_array = &nw::kernel::rules().classes;
    std::string temp_string;
    if (tda.get_to("label", temp_string)) {
        tda.get_to("Name", name);
        tda.get_to("Plural", plural);
        tda.get_to("Lower", lower);
        tda.get_to("Description", description);
        if (tda.get_to("Icon", temp_string)) {
            icon = {temp_string, nw::ResourceType::texture};
        }
        tda.get_to("HitDie", hitdie);
        if (tda.get_to("AttackBonusTable", temp_string)) {
            TwoDA ab_2da(nw::kernel::resman().demand({temp_string, nw::ResourceType::twoda}));
            std::vector<int> ab;
            if (ab_2da.is_valid()) {
                for (size_t i = 0; i < ab_2da.rows(); ++i) {
                    int temp;
                    if (ab_2da.get_to(i, "BAB", temp)) {
                        ab.push_back(temp);
                    }
                }
                attack_bonus_table = &*class_array->attack_tables.insert(ab).first;
            }
        }
        if (tda.get_to("FeatsTable", temp_string)) {
            feats_table = {temp_string, nw::ResourceType::twoda};
        }
        if (tda.get_to("SavingThrowTable", temp_string)) {
            saving_throw_table = {temp_string, nw::ResourceType::twoda};
        }
        if (tda.get_to("SkillsTable", temp_string)) {
            skill_table = {temp_string, nw::ResourceType::twoda};
        }
        if (tda.get_to("BonusFeatsTable", temp_string)) {
            bonus_feats_table = {temp_string, nw::ResourceType::twoda};
        }
        tda.get_to("SkillPointBase", skill_point_base);
        if (tda.get_to("SpellGainTable", temp_string)) {
            spell_gain_table = {temp_string, nw::ResourceType::twoda};
        }
        if (tda.get_to("SpellKnownTable", temp_string)) {
            spell_known_table = {temp_string, nw::ResourceType::twoda};
        }
        tda.get_to("PlayerClass", player_class);
        tda.get_to("SpellCaster", spellcaster);

        tda.get_to("PrimaryAbil", primary_ability);
        tda.get_to("AlignRestrict", alignment_restriction);
        tda.get_to("AlignRstrctType", alignment_restriction_type);
        tda.get_to("InvertRestrict", invert_restriction);
        if (tda.get_to("Constant", temp_string)) {
            constant = nw::kernel::strings().intern(temp_string);
        }
        if (tda.get_to("PreReqTable", temp_string)) {
            prereq_table = {temp_string, nw::ResourceType::twoda};
        }
        tda.get_to("MaxLevel", max_level);
        tda.get_to("XPPenalty", xp_penalty);

        if (!spellcaster) {
            tda.get_to("ArcSpellLvlMod", arcane_spellgain_mod);
            tda.get_to("DivSpellLvlMod", divine_spellgain_mod);
        }

        tda.get_to("EpicLevel", epic_level_limit);
        tda.get_to("Package", package);
        if (tda.get_to("StatGainTable", temp_string)) {
            stat_gain_table = {temp_string, nw::ResourceType::twoda};
        }

        // No point in checking for anyone else
        if (spellcaster) {
            tda.get_to("MemorizesSpells", memorizes_spells);
            tda.get_to("SpellbookRestricted", spellbook_restricted);
            tda.get_to("PickDomains", pick_domains);
            tda.get_to("PickSchool", pick_school);
            tda.get_to("LearnScroll", learn_scroll);
            tda.get_to("Arcane", arcane);
            tda.get_to("ASF", arcane_spell_failure);
            tda.get_to("SpellcastingAbil", caster_ability);
            tda.get_to("SpellTableColumn", spell_table_column);
            tda.get_to("CLMultiplier", caster_level_multiplier);
            tda.get_to("MinCastingLevel", level_min_caster);
            tda.get_to("MinAssociateLevel", level_min_associate);
            tda.get_to("CanCastSpontaneously", can_cast_spontaneously);
        }
    }
}

Class ClassArray::from_constant(std::string_view constant) const
{
    absl::string_view v{constant.data(), constant.size()};
    auto it = constant_to_index.find(v);
    if (it == constant_to_index.end()) {
        return Class::invalid();
    } else {
        return it->second;
    }
}

const ClassInfo* ClassArray::get(Class type) const noexcept
{
    if (type.idx() < entries.size() && entries[type.idx()].valid()) {
        return &entries[type.idx()];
    } else {
        return nullptr;
    }
}

int ClassArray::get_base_attack_bonus(Class class_, size_t level) const
{
    level -= 1;
    if (!is_valid(class_)) { return 0; }
    auto& info = entries[class_.idx()];
    if (info.attack_bonus_table && level < info.attack_bonus_table->size()) {
        return (*info.attack_bonus_table)[level];
    }
    return 0;
}

Saves ClassArray::get_class_save_bonus(Class class_, size_t level) const
{
    auto info = get(class_);
    if (info && level - 1 < info->class_saves.size()) {
        return info->class_saves[level - 1];
    }
    return {};
}

bool ClassArray::get_is_class_skill(Class class_, Skill skill) const
{
    auto info = get(class_);
    if (info && skill.idx() < info->class_skills.size()) {
        return !!info->class_skills[skill.idx()];
    }
    return false;
}

int ClassArray::get_natural_ac(Class class_, size_t level) const
{
    int result = 0;
    auto info = get(class_);
    if (info && level - 1 < info->class_stat_gain.size()) {
        for (size_t i = 0; i < level; ++i) {
            result += info->class_stat_gain[i].natural_ac;
        }
    }
    return result;
}

const ClassRequirement* ClassArray::get_requirement(Class class_) const
{
    if (auto info = get(class_)) {
        return &info->requirements;
    }
    return nullptr;
}

int ClassArray::get_stat_gain(Class class_, Ability ability, size_t level) const
{
    if (ability == nw::Ability::invalid()) { return 0; }
    int result = 0;
    auto info = get(class_);
    if (info && level - 1 < info->class_stat_gain.size()) {
        // Wild this isn't additive like every other class table
        for (size_t i = 0; i < level; ++i) {
            result += info->class_stat_gain[i].ability[ability.idx()];
        }
    }
    return result;
}

bool ClassArray::is_valid(Class type) const noexcept
{
    return type.idx() < entries.size() && entries[type.idx()].valid();
}

} // namespace nw
