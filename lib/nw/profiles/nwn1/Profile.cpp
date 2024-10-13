#include "Profile.hpp"

#include "constants.hpp"
#include "effects.hpp"
#include "functions.hpp"
#include "rules.hpp"

#include "../../formats/Ini.hpp"
#include "../../formats/TwoDA.hpp"
#include "../../kernel/Objects.hpp"
#include "../../kernel/Resources.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"
#include "../../rules/feats.hpp"
#include "../../rules/system.hpp"

namespace nwk = nw::kernel;

using namespace std::literals;

namespace nwn1 {

bool Profile::load_rules() const
{
    LOG_F(INFO, "[nwn1] loading rules...");

    // == Load Effects ========================================================
    load_effects();
    load_itemprop_generators();

    // == Load Rules ==========================================================

    nw::AttackFuncs acb;
    acb.resolve_attack = resolve_attack;
    acb.resolve_attack_bonus = resolve_attack_bonus;
    acb.resolve_attack_damage = resolve_attack_damage;
    acb.resolve_attack_roll = resolve_attack_roll;
    acb.resolve_attack_type = resolve_attack_type;
    acb.resolve_concealment = resolve_concealment;
    acb.resolve_critical_multiplier = resolve_critical_multiplier;
    acb.resolve_critical_threat = resolve_critical_threat;
    acb.resolve_damage_modifiers = resolve_damage_modifiers;
    acb.resolve_damage_immunity = resolve_damage_immunity;
    acb.resolve_damage_reduction = resolve_damage_reduction;
    acb.resolve_damage_resistance = resolve_damage_resistance;
    acb.resolve_dual_wield_penalty = resolve_dual_wield_penalty;
    acb.resolve_iteration_penalty = resolve_iteration_penalty;
    acb.resolve_number_of_attacks = resolve_number_of_attacks;
    acb.resolve_target_state = resolve_target_state;
    acb.resolve_weapon_damage_flags = resolve_weapon_damage_flags;
    acb.resolve_weapon_power = resolve_weapon_power;
    nwk::rules().set_attack_functions(acb);

    load_combat_modes();
    load_modifiers();
    load_master_feats();
    load_special_attacks();
    load_qualifiers();

    nw::kernel::objects().set_instantiate_callback([](nw::ObjectBase* obj) {
        switch (obj->handle().type) {
        default:
            return;
        case nw::ObjectType::creature: {
            auto cre = obj->as_creature();
            cre->hp_max = cre->hp_current = get_max_hitpoints(cre);
        }
        }
    });

    // == Load 2das ===========================================================

    nw::TwoDA appearances{nw::kernel::resman().demand({"appearance"sv, nw::ResourceType::twoda})};
    nw::TwoDA baseitems{nw::kernel::resman().demand({"baseitems"sv, nw::ResourceType::twoda})};
    nw::TwoDA classes{nw::kernel::resman().demand({"classes"sv, nw::ResourceType::twoda})};
    nw::TwoDA feat{nw::kernel::resman().demand({"feat"sv, nw::ResourceType::twoda})};
    nw::TwoDA placeables{nw::kernel::resman().demand({"placeables"sv, nw::ResourceType::twoda})};
    nw::TwoDA phenotypes{nw::kernel::resman().demand({"phenotype"sv, nw::ResourceType::twoda})};
    nw::TwoDA racialtypes{nw::kernel::resman().demand({"racialtypes"sv, nw::ResourceType::twoda})};
    nw::TwoDA skills{nw::kernel::resman().demand({"skills"sv, nw::ResourceType::twoda})};
    nw::TwoDA spells{nw::kernel::resman().demand({"spells"sv, nw::ResourceType::twoda})};
    nw::TwoDA spellschools{nw::kernel::resman().demand({"spellschools"sv, nw::ResourceType::twoda})};
    nw::TwoDA traps{nw::kernel::resman().demand({"traps"sv, nw::ResourceType::twoda})};
    nw::String temp_string;
    int temp_int = 0;

    auto& appearance_array = nw::kernel::rules().appearances;
    if (appearances.is_valid()) {
        appearance_array.entries.reserve(appearances.rows());
        for (size_t i = 0; i < appearances.rows(); ++i) {
            appearance_array.entries.emplace_back(appearances.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'appearance.2da'");
    }

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
                        nw::StringView temp;
                        if (!tda.get_to(j, "ReqType", temp)) { continue; }
                        int param1_int = 0;
                        int param2_int = 0;

                        if (nw::string::icmp("ARCSPELL", temp)) {
                            // [TODO]
                        } else if (nw::string::icmp("BAB", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.main.add(
                                    nw::qualifier_base_attack_bonus(param1_int));
                            }
                        } else if (nw::string::icmp("CLASSOR", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.class_or.add(
                                    nw::qualifier_class_level(nw::Class::make(param1_int), 1));
                            }
                        } else if (nw::string::icmp("CLASSNOT", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.class_not.add(
                                    nw::qualifier_class_level(nw::Class::make(param1_int), 1));
                            }
                        } else if (nw::string::icmp("FEAT", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.main.add(
                                    nw::qualifier_feat(nw::Feat::make(param1_int)));
                            }
                        } else if (nw::string::icmp("FEATOR", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.feat_or.add(
                                    nw::qualifier_feat(nw::Feat::make(param1_int)));
                            }
                        } else if (nw::string::icmp("RACE", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)) {
                                info.requirements.main.add(
                                    nw::qualifier_race(nw::Race::make(param1_int)));
                            }
                        } else if (nw::string::icmp("SAVE", temp)) {
                            // [TODO]
                        } else if (nw::string::icmp("SKILL", temp)) {
                            if (tda.get_to(j, "ReqParam1", param1_int)
                                && tda.get_to(j, "ReqParam2", param2_int)) {
                                info.requirements.main.add(
                                    nw::qualifier_skill(nw::Skill::make(param1_int), param2_int));
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

    // Placeables
    auto& placeable_array = nw::kernel::rules().placeables;
    if (placeables.is_valid()) {
        placeable_array.entries.reserve(placeables.rows());
        for (size_t i = 0; i < placeables.rows(); ++i) {
            placeable_array.entries.emplace_back(placeables.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'placeables.2da'");
    }

    auto& phenotype_array = nw::kernel::rules().phenotypes;
    if (phenotypes.is_valid()) {
        phenotype_array.entries.reserve(phenotypes.rows());
        for (size_t i = 0; i < phenotypes.rows(); ++i) {
            phenotype_array.entries.emplace_back(phenotypes.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'phenotype.2da'");
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

    // Spell Schools
    auto& spell_school_array = nw::kernel::rules().spellschools;
    if (spellschools.is_valid()) {
        spell_school_array.entries.reserve(spellschools.rows());
        for (size_t i = 0; i < spellschools.rows(); ++i) {
            spell_school_array.entries.emplace_back(spellschools.row(i));
        }
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

    // Traps
    auto& trap_array = nw::kernel::rules().traps;
    if (traps.is_valid()) {
        trap_array.entries.reserve(traps.rows());
        for (size_t i = 0; i < traps.rows(); ++i) {
            trap_array.entries.emplace_back(traps.row(i));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'traps.2da'");
    }

    // == Postprocess 2das ====================================================

    // BaseItems
    for (size_t i = 0; i < baseitem_array.entries.size(); ++i) {
        nw::BaseItemInfo& info = baseitem_array.entries[i];
        if (baseitems.get_to(i, "ReqFeat0", temp_int)) {
            info.feat_requirement.add(nw::qualifier_feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat1", temp_int)) {
            info.feat_requirement.add(nw::qualifier_feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat2", temp_int)) {
            info.feat_requirement.add(nw::qualifier_feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat3", temp_int)) {
            info.feat_requirement.add(nw::qualifier_feat(nw::Feat::make(temp_int)));
        }

        if (baseitems.get_to(i, "ReqFeat4", temp_int)) {
            info.feat_requirement.add(nw::qualifier_feat(nw::Feat::make(temp_int)));
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
            info.requirements.add(nw::qualifier_level(0, temp_int));
        }

        if (feat.get_to(i, "MINSTR", temp_int)) {
            info.requirements.add(nw::qualifier_ability(ability_strength, temp_int));
        }

        if (feat.get_to(i, "MINDEX", temp_int)) {
            info.requirements.add(nw::qualifier_ability(ability_dexterity, temp_int));
        }

        if (feat.get_to(i, "MINCON", temp_int)) {
            info.requirements.add(nw::qualifier_ability(ability_constitution, temp_int));
        }

        if (feat.get_to(i, "MININT", temp_int)) {
            info.requirements.add(nw::qualifier_ability(ability_intelligence, temp_int));
        }

        if (feat.get_to(i, "MINWIS", temp_int)) {
            info.requirements.add(nw::qualifier_ability(ability_wisdom, temp_int));
        }

        if (feat.get_to(i, "MINCHA", temp_int)) {
            info.requirements.add(nw::qualifier_ability(ability_charisma, temp_int));
        }

        if (feat.get_to(i, "PREREQFEAT1", temp_int)) {
            info.requirements.add(nw::qualifier_feat(nw::Feat::make(temp_int)));
        }

        if (feat.get_to(i, "PREREQFEAT2", temp_int)) {
            info.requirements.add(nw::qualifier_feat(nw::Feat::make(temp_int)));
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

    return true;
}

bool Profile::load_resources()
{
    bool include_user = nwk::config().options().include_user;
    bool include_install = nwk::config().options().include_install;
    auto version = nwk::config().version();

    // Overrides
    if (include_user && version == nw::GameVersion::vEE) {
        nwk::resman().add_override_container(nwk::config().user_path(), "development");
    }

    if (include_user) {
        nwk::resman().add_override_container(nwk::config().user_path(), "portraits",
            nw::ResourceType::texture);
    }

    if (include_install) {
        nwk::resman().add_override_container(nwk::config().install_path() / "data", "prt",
            nw::ResourceType::texture);
    }

    // Base
    if (include_user) {
        nwk::resman().add_base_container(nwk::config().user_path(), "override",
            nw::ResourceType::texture);
    }

    if (include_install) {
        nwk::resman().add_base_container(nwk::config().install_path() / "data", "ovr",
            nw::ResourceType::texture);
    }

    if (include_user) {
        nwk::resman().add_base_container(nwk::config().user_path(), "ambient",
            nw::ResourceType::sound);
        nwk::resman().add_base_container(nwk::config().user_path(), "music",
            nw::ResourceType::sound);
    }

    if (include_install) {
        nwk::resman().add_base_container(nwk::config().install_path() / "data", "amb",
            nw::ResourceType::sound);
        nwk::resman().add_base_container(nwk::config().install_path() / "data", "mus",
            nw::ResourceType::sound);
    }

    if (include_user) {
        if (std::filesystem::exists(nwk::config().user_path() / "userpatch.ini")) {
            nw::Ini user_patch{nwk::config().user_path() / "userpatch.ini"};
            if (user_patch.valid()) {
                int i = 0;
                nw::String file;
                while (user_patch.get_to(fmt::format("Patch/PatchFile{:03d}", i++), file)) {
                    if (!nwk::resman().add_base_container(nwk::config().user_path() / "patch", file)) {
                        break;
                    }
                }
            }
        }
    }

    if (include_install) {
        if (version == nw::GameVersion::vEE) {
            auto lang = nwk::strings().global_language();
            if (lang != nw::LanguageID::english) {
                auto shortcode = nw::Language::to_string(lang);
                nwk::resman().add_base_container(
                    nwk::config().install_path() / "lang" / shortcode / "data", "nwn_base_loc");
            }
            nwk::resman().add_base_container(
                nwk::config().install_path() / "data", "nwn_base");

        } else {
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "xp2_tex_tpa");
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "xp1_tex_tpa");
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "textures_tpa");
            nwk::resman().add_base_container(nwk::config().install_path() / "texturepacks", "tiles_tpa");

            nwk::resman().add_base_container(nwk::config().install_path(), "xp3patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp3");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp2patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp2");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp1patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "xp1");
            nwk::resman().add_base_container(nwk::config().install_path(), "patch");
            nwk::resman().add_base_container(nwk::config().install_path(), "chitin");
        }
    }

    return true;
}

} // namespace nwn1
