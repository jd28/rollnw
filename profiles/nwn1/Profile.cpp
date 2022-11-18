#include "Profile.hpp"

#include "constants.hpp"
#include "constants/const_feat.hpp"
#include "effects.hpp"
#include "functions/rule_helper_funcs.hpp"
#include "nw/rules/system.hpp"
#include "rules.hpp"

#include <nw/components/Creature.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/rules/feats.hpp>

namespace nwn1 {

static inline void load_effects()
{
    LOG_F(INFO, "[nwn1] Loading effect appliers");

#define ADD_IS_CREATURE_EFFECT(type) \
    nw::kernel::effects().add(type, effect_apply_is_creature, effect_remove_is_creature)

    ADD_IS_CREATURE_EFFECT(effect_type_ability_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_ability_decrease);

    ADD_IS_CREATURE_EFFECT(effect_type_attack_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_attack_decrease);

    nw::kernel::effects().add(effect_type_haste, effect_haste_apply, effect_haste_remove);

    ADD_IS_CREATURE_EFFECT(effect_type_skill_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_skill_decrease);

#undef ADD_IS_CREATURE_EFFECT
}

static inline void load_itemprop_generators()
{
    LOG_F(INFO, "[nwn1] Loading item property generators");
    nw::kernel::effects().add(ip_ability_bonus, ip_gen_ability_modifier);
    nw::kernel::effects().add(ip_decreased_ability_score, ip_gen_ability_modifier);
    nw::kernel::effects().add(ip_haste, ip_gen_haste);
    nw::kernel::effects().add(ip_skill_bonus, ip_gen_skill_modifier);
    nw::kernel::effects().add(ip_decreased_skill_modifier, ip_gen_skill_modifier);
}

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
    // ADD_SKILL(appraise);
    ADD_SKILL(tumble);
    ADD_SKILL(craft_trap);
    ADD_SKILL(bluff);
    ADD_SKILL(intimidate);
    ADD_SKILL(craft_armor);
    ADD_SKILL(craft_weapon);
    // ADD_SKILL(ride);

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

    // Special case baseitem invalid
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_focus, feat_weapon_focus_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_focus_epic, feat_epic_weapon_focus_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_spec, feat_weapon_specialization_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_spec_epic, feat_epic_devastating_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_devastating_crit, feat_epic_devastating_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_improved_crit, feat_improved_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_overwhelming_crit, feat_epic_overwhelming_critical_unarmed);

    return true;
}

static inline void load_modifiers()
{
    LOG_F(INFO, "[nwn1] loading modifiers...");

    auto& rules = nw::kernel::rules();

    // Ability
    rules.modifiers.add(mod::ability(
        epic_great_ability,
        "dnd-3.0-epic-great-ability",
        nw::ModifierSource::feat));

    // Armor Class
    rules.modifiers.add(mod::armor_class(
        ac_natural,
        dragon_disciple_ac,
        "dnd-3.0-dragon-disciple-ac",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::armor_class(
        ac_natural,
        pale_master_ac,
        "dnd-3.0-palemaster-ac",
        nw::ModifierSource::class_));

    // Attack Bonus
    rules.modifiers.add(mod::attack_bonus_item(
        enchant_arrow_ab,
        "dnd-3.0-enchange-arrow",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_any,
        simple_feat_mod(feat_epic_prowess, 1),
        "dnd-3.0-epic-prowess",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_any,
        target_state_ab,
        "dnd-3.0-target_state",
        nw::ModifierSource::unknown));

    rules.modifiers.add(mod::attack_bonus_item(
        weapon_master_ab,
        "dnd-3.0-weaponmaster-ab",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::attack_bonus_mode(
        combat_mode_expertise,
        -5,
        "dnd-3.0-rapid_shot-ab",
        nw::ModifierSource::combat_mode));

    rules.modifiers.add(mod::attack_bonus_mode(
        combat_mode_improved_expertise,
        -10,
        "dnd-3.0-rapid_shot-ab",
        nw::ModifierSource::combat_mode));

    rules.modifiers.add(mod::attack_bonus_mode(
        combat_mode_flurry_of_blows,
        -2,
        "dnd-3.0-rapid_shot-ab",
        nw::ModifierSource::combat_mode));

    rules.modifiers.add(mod::attack_bonus_mode(
        combat_mode_power_attack,
        -5,
        "dnd-3.0-rapid_shot-ab",
        nw::ModifierSource::combat_mode));

    rules.modifiers.add(mod::attack_bonus_mode(
        combat_mode_improved_power_attack,
        -10,
        "dnd-3.0-rapid_shot-ab",
        nw::ModifierSource::combat_mode));

    rules.modifiers.add(mod::attack_bonus_mode(
        combat_mode_rapid_shot,
        -2,
        "dnd-3.0-rapid_shot-ab",
        nw::ModifierSource::combat_mode));

    // Damage Resist
    rules.modifiers.add(mod::damage_resist(
        energy_resistance,
        "dnd-3.0-energy-resist-acid",
        nw::ModifierSource::feat));

    // Hitpoints
    rules.modifiers.add(mod::hitpoints(
        toughness,
        "dnd-3.0-toughness",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::hitpoints(
        epic_toughness,
        "dnd-3.0-epic-toughness",
        nw::ModifierSource::feat));

    // Skills
    rules.modifiers.add(mod::skill(
        skill_search,
        simple_feat_mod(feat_stonecunning, 2),
        "dnd-3.0-stone-cunning",
        nw::ModifierSource::feat));
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
            const auto& info = class_array.entries.emplace_back(classes.row(i));
            if (info.constant) {
                class_array.constant_to_index.emplace(info.constant, nw::Class::make(int32_t(i)));
            } else if (info.valid()) {
                LOG_F(WARNING, "[nwn1] valid class ({}) with invalid constant", i);
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
