#include "Profile.hpp"

#include "modifiers.hpp"
#include "rules.hpp"

#include <nw/components/Creature.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
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
