#include "Profile.hpp"

#include "constants.hpp"
#include "effects.hpp"
#include "helpers.hpp"
#include "modifiers.hpp"
#include "rules.hpp"

#include <nw/formats/TwoDA.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/system.hpp>

namespace nwn1 {

static inline bool load_master_feats()
{
    LOG_F(INFO, "[nwn1] Loading master feats");

    auto& mfr = nw::kernel::rules().master_feats;
    mfr.set_bonus(mfeat_skill_focus, 3);
    mfr.set_bonus(mfeat_skill_focus_epic, 10);

#define ADD_SKILL(name)                                                \
    mfr.add(skill_##name, mfeat_skill_focus, feat_skill_focus_##name); \
    mfr.add(skill_##name, mfeat_skill_focus_epic, feat_epic_skill_focus_##name)

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
    ADD_SKILL(appraise);
    ADD_SKILL(tumble);
    ADD_SKILL(craft_trap);
    ADD_SKILL(bluff);
    ADD_SKILL(intimidate);
    ADD_SKILL(craft_armor);
    ADD_SKILL(craft_weapon);
    // ADD_SKILL(ride); -- No feats

#undef ADD_SKILL

    // Weapons
    mfr.set_bonus(mfeat_weapon_focus, 1);
    mfr.set_bonus(mfeat_weapon_focus_epic, 2);
    mfr.set_bonus(mfeat_weapon_spec, 2);
    mfr.set_bonus(mfeat_weapon_spec_epic, 4);
    // [TODO] One here is just an indicator that char has feat.. for now.  Ultimately, dice rolls
    // and damage rolls will need to be included in ModifierResult
    mfr.set_bonus(mfeat_devastating_crit, 1);
    mfr.set_bonus(mfeat_improved_crit, 1);
    mfr.set_bonus(mfeat_overwhelming_crit, 1);
    mfr.set_bonus(mfeat_weapon_of_choice, 1);

    // Special case baseitem invalid - the rest will be loaded during the baseitem loading below
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_focus, feat_weapon_focus_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_focus_epic, feat_epic_weapon_focus_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_spec, feat_weapon_specialization_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_spec_epic, feat_epic_devastating_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_devastating_crit, feat_epic_devastating_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_improved_crit, feat_improved_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_overwhelming_crit, feat_epic_overwhelming_critical_unarmed);

    // Favored Enemy - Bonus is just a check for having the feat.
    mfr.set_bonus(mfeat_favored_enemy, 1);
    mfr.add(racial_type_dwarf, mfeat_favored_enemy, feat_favored_enemy_dwarf);
    mfr.add(racial_type_elf, mfeat_favored_enemy, feat_favored_enemy_elf);
    mfr.add(racial_type_gnome, mfeat_favored_enemy, feat_favored_enemy_gnome);
    mfr.add(racial_type_halfling, mfeat_favored_enemy, feat_favored_enemy_halfling);
    mfr.add(racial_type_halfelf, mfeat_favored_enemy, feat_favored_enemy_halfelf);
    mfr.add(racial_type_halforc, mfeat_favored_enemy, feat_favored_enemy_halforc);
    mfr.add(racial_type_human, mfeat_favored_enemy, feat_favored_enemy_human);
    mfr.add(racial_type_aberration, mfeat_favored_enemy, feat_favored_enemy_aberration);
    mfr.add(racial_type_animal, mfeat_favored_enemy, feat_favored_enemy_animal);
    mfr.add(racial_type_beast, mfeat_favored_enemy, feat_favored_enemy_beast);
    mfr.add(racial_type_construct, mfeat_favored_enemy, feat_favored_enemy_construct);
    mfr.add(racial_type_dragon, mfeat_favored_enemy, feat_favored_enemy_dragon);
    mfr.add(racial_type_humanoid_goblinoid, mfeat_favored_enemy, feat_favored_enemy_goblinoid);
    mfr.add(racial_type_humanoid_monstrous, mfeat_favored_enemy, feat_favored_enemy_monstrous);
    mfr.add(racial_type_humanoid_orc, mfeat_favored_enemy, feat_favored_enemy_orc);
    mfr.add(racial_type_humanoid_reptilian, mfeat_favored_enemy, feat_favored_enemy_reptilian);
    mfr.add(racial_type_elemental, mfeat_favored_enemy, feat_favored_enemy_elemental);
    mfr.add(racial_type_fey, mfeat_favored_enemy, feat_favored_enemy_fey);
    mfr.add(racial_type_giant, mfeat_favored_enemy, feat_favored_enemy_giant);
    mfr.add(racial_type_magical_beast, mfeat_favored_enemy, feat_favored_enemy_magical_beast);
    mfr.add(racial_type_outsider, mfeat_favored_enemy, feat_favored_enemy_outsider);
    mfr.add(racial_type_shapechanger, mfeat_favored_enemy, feat_favored_enemy_shapechanger);
    mfr.add(racial_type_undead, mfeat_favored_enemy, feat_favored_enemy_undead);
    mfr.add(racial_type_vermin, mfeat_favored_enemy, feat_favored_enemy_vermin);

    LOG_F(INFO, "  ... {} master feat specializations loaded", mfr.entries().size());
    return true;
}

bool Profile::load_rules() const
{
    LOG_F(INFO, "[nwn1] loading rules...");

    // == Set global rule functions ===========================================
    nw::kernel::rules().set_selector(selector);
    nw::kernel::rules().set_qualifier(match);

    // == Load 2das ===========================================================

    nw::TwoDA baseitems{nw::kernel::resman().demand({"baseitems"sv, nw::ResourceType::twoda})};
    nw::TwoDA classes{nw::kernel::resman().demand({"classes"sv, nw::ResourceType::twoda})};
    nw::TwoDA feat{nw::kernel::resman().demand({"feat"sv, nw::ResourceType::twoda})};
    nw::TwoDA racialtypes{nw::kernel::resman().demand({"racialtypes"sv, nw::ResourceType::twoda})};
    nw::TwoDA skills{nw::kernel::resman().demand({"skills"sv, nw::ResourceType::twoda})};
    nw::TwoDA spells{nw::kernel::resman().demand({"spells"sv, nw::ResourceType::twoda})};
    std::string temp_string;
    int temp_int = 0;

    // BaseItems
    auto& baseitem_array = nw::kernel::rules().baseitems;
    auto& mfr = nw::kernel::rules().master_feats;
    if (baseitems.is_valid()) {
        for (size_t i = 0; i < baseitems.rows(); ++i) {
            auto& info = baseitem_array.entries.emplace_back(baseitems.row(i));
            if (info.valid()) {
                if (baseitems.get_to(i, "WeaponFocusFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_weapon_focus, nw::Feat::make(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponFocusFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_weapon_focus_epic, nw::Feat::make(temp_int));
                }
                if (baseitems.get_to(i, "WeaponSpecializationFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_weapon_spec, nw::Feat::make(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponSpecializationFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_weapon_spec_epic, nw::Feat::make(temp_int));
                }
                if (baseitems.get_to(i, "WeaponImprovedCriticalFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_improved_crit, nw::Feat::make(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponOverwhelmingCriticalFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_overwhelming_crit, nw::Feat::make(temp_int));
                }
                if (baseitems.get_to(i, "EpicWeaponDevastatingCriticalFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_devastating_crit, nw::Feat::make(temp_int));
                }
                if (baseitems.get_to(i, "WeaponOfChoiceFeat", temp_int)) {
                    mfr.add(nw::BaseItem::make(static_cast<int32_t>(i)),
                        mfeat_weapon_of_choice, nw::Feat::make(temp_int));
                }
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'baseitems.2da'");
    }

    // Classes
    auto& class_array = nw::kernel::rules().classes;
    if (classes.is_valid()) {
        class_array.entries.reserve(classes.rows());
        for (size_t i = 0; i < classes.rows(); ++i) {
            auto& info = class_array.entries.emplace_back(classes.row(i));
            if (info.constant) {
                class_array.constant_to_index.emplace(info.constant, nw::Class::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid class ({}) with invalid constant", i);
            }
            if (info.skill_table.valid()) {
                nw::TwoDA tda{nw::kernel::resman().demand(info.skill_table)};
                if (tda.is_valid()) {
                    info.class_skills.resize(skills.rows());
                    for (size_t j = 0; j < tda.rows(); ++j) {
                        size_t index;
                        int is_class;
                        if (tda.get_to(j, "SkillIndex", index)
                            && tda.get_to(j, "ClassSkill", is_class)) {
                            if (index >= info.class_skills.size()) {
                                LOG_F(ERROR, "class array: invalid skill index {} on row {}", index, j);
                                continue;
                            }
                            info.class_skills[index] = is_class;
                        }
                    }
                }
            }
            if (info.saving_throw_table.valid()) {
                nw::TwoDA tda{nw::kernel::resman().demand(info.saving_throw_table)};
                if (tda.is_valid()) {
                    info.class_saves.reserve(tda.rows());
                    for (size_t j = 0; j < tda.rows(); ++j) {
                        nw::Saves save;

                        if (!tda.get_to(j, "FortSave", save.fort)
                            || !tda.get_to(j, "RefSave", save.reflex)
                            || !tda.get_to(j, "WillSave", save.will)) {
                            LOG_F(ERROR, "class array: invalid save on row {}", j);
                        }
                        info.class_saves.push_back(save);
                    }
                }
            }
            if (info.stat_gain_table.valid()) {
                nw::TwoDA tda{nw::kernel::resman().demand(info.stat_gain_table)};
                if (tda.is_valid()) {
                    info.class_stat_gain.reserve(tda.rows());
                    for (size_t j = 0; j < tda.rows(); ++j) {
                        nw::ClassStatGain result{};
                        tda.get_to(j, "Str", result.ability[0]);
                        tda.get_to(j, "Dex", result.ability[1]);
                        tda.get_to(j, "Con", result.ability[2]);
                        tda.get_to(j, "Wis", result.ability[3]);
                        tda.get_to(j, "Int", result.ability[4]);
                        tda.get_to(j, "Cha", result.ability[5]);
                        tda.get_to(j, "NaturalAC", result.natural_ac);
                        info.class_stat_gain.push_back(result);
                    }
                }
            }
            if (info.prereq_table.valid()) {
                nw::TwoDA tda{nw::kernel::resman().demand(info.prereq_table)};
                if (tda.is_valid()) {
                    for (size_t j = 0; j < tda.rows(); ++j) {
                        std::string_view temp;
                        if (!tda.get_to(j, "ReqType", temp)) { continue; }
                        int param1_int = 0;
                        int param2_int = 0;

                        if (nw::string::icmp("ARCSPELL", temp)) {
                            // [TODO]
                        } else if (nw::string::icmp("BAB", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.main.add(
                                    qual::base_attack_bonus(param1_int));
                            }
                        } else if (nw::string::icmp("CLASSOR", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.class_or.add(
                                    qual::class_level(nw::Class::make(param1_int), 1));
                            }
                        } else if (nw::string::icmp("CLASSNOT", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.class_not.add(
                                    qual::class_level(nw::Class::make(param1_int), 1));
                            }
                        } else if (nw::string::icmp("FEAT", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.main.add(
                                    qual::feat(nw::Feat::make(param1_int)));
                            }
                        } else if (nw::string::icmp("FEATOR", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.feat_or.add(
                                    qual::feat(nw::Feat::make(param1_int)));
                            }
                        } else if (nw::string::icmp("RACE", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.main.add(
                                    qual::race(nw::Race::make(param1_int)));
                            }
                        } else if (nw::string::icmp("SAVE", temp)) {
                            // [TODO]
                        } else if (nw::string::icmp("SKILL", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)
                                && tda.get_to(j, "ReqParam2", param2_int)) {
                                info.requirements.main.add(
                                    qual::skill(nw::Skill::make(param1_int), param2_int));
                            }
                        } else if (nw::string::icmp("SPELL", temp)) {
                            // [TODO]
                        } else if (nw::string::icmp("VAR", temp)) {
                            // [TODO]
                        } else {
                            LOG_F(ERROR, "class array: unknown requirement");
                        }
                    }
                }
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'classes.2da'");
    }

    // Feats
    auto& feat_array = nw::kernel::rules().feats;
    if (feat.is_valid()) {
        feat_array.entries.reserve(feat.rows());
        for (size_t i = 0; i < feat.rows(); ++i) {
            const auto& info = feat_array.entries.emplace_back(feat.row(i));
            if (info.constant) {
                feat_array.constant_to_index.emplace(info.constant, nw::Feat::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid feat ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'feat.2da'");
    }

    // Races
    auto& race_array = nw::kernel::rules().races;
    if (racialtypes.is_valid()) {
        race_array.entries.reserve(racialtypes.rows());
        for (size_t i = 0; i < racialtypes.rows(); ++i) {
            const auto& info = race_array.entries.emplace_back(racialtypes.row(i));
            if (info.constant) {
                race_array.constant_to_index.emplace(info.constant, nw::Race::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid race ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'racialtypes.2da'");
    }

    // Skills
    auto& skill_array = nw::kernel::rules().skills;
    if (skills.is_valid()) {
        skill_array.entries.reserve(skills.rows());
        for (size_t i = 0; i < skills.rows(); ++i) {
            const auto& info = skill_array.entries.emplace_back(skills.row(i));
            if (info.constant) {
                skill_array.constant_to_index.emplace(info.constant, nw::Skill::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid skill ({}) with invalid constant", i);
            }
        }
    } else {
        throw std::runtime_error("rules: failed to load 'skills.2da'");
    }

    // Spells
    auto& spell_array = nw::kernel::rules().spells;
    if (spells.is_valid()) {
        spell_array.entries.reserve(spells.rows());
        for (size_t i = 0; i < spells.rows(); ++i) {
            spell_array.entries.emplace_back(spells.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'spells.2da'");
    }

    // == Postprocess 2das ====================================================

    // BaseItems
    for (size_t i = 0; i < baseitem_array.entries.size(); ++i) {
        nw::BaseItemInfo& info = baseitem_array.entries[i];
        if (baseitems.get_to(i, "ReqFeat0", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat1", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat2", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat3", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat4", temp_int)) {
            info.feat_requirement.add(qual::feat(nw::Feat::make(temp_int)));
        }
    }

    // Classes

    // Feats

    for (size_t i = 0; i < feat_array.entries.size(); ++i) {
        nw::FeatInfo& info = feat_array.entries[i];
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
            info.requirements.add(qual::feat(nw::Feat::make(temp_int)));
        }

        if (feat.get_to(i, "PREREQFEAT2", temp_int)) {
            info.requirements.add(qual::feat(nw::Feat::make(temp_int)));
        }
    }

    // Races

    // Skill
    for (size_t i = 0; i < skills.rows(); ++i) {
        auto& info = skill_array.entries[i];
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

    // == Load Effects ========================================================
    load_effects();

    // == Load Item Property Generators =======================================
    load_itemprop_generators();

    return true;
}

} // namespace nwn1
