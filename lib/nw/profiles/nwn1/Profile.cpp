#include "Profile.hpp"

#include "../../formats/TwoDA.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/components/ConstantRegistry.hpp"
#include "../../objects/Creature.hpp"
#include "../../rules/Ability.hpp"
#include "../../rules/BaseItem.hpp"
#include "../../rules/Class.hpp"
#include "../../rules/Feat.hpp"
#include "../../rules/Race.hpp"
#include "../../rules/Skill.hpp"
#include "../../rules/Spell.hpp"
#include "../../rules/system.hpp"

namespace nwn1 {

nw::Constant ability_strength;
nw::Constant ability_dexterity;
nw::Constant ability_constitution;
nw::Constant ability_intelligence;
nw::Constant ability_wisdom;
nw::Constant ability_charisma;

nw::Constant racial_type_dwarf;
nw::Constant racial_type_elf;
nw::Constant racial_type_gnome;
nw::Constant racial_type_halfling;
nw::Constant racial_type_halfelf;
nw::Constant racial_type_halforc;
nw::Constant racial_type_human;
nw::Constant racial_type_aberration;
nw::Constant racial_type_animal;
nw::Constant racial_type_beast;
nw::Constant racial_type_construct;
nw::Constant racial_type_dragon;
nw::Constant racial_type_humanoid_goblinoid;
nw::Constant racial_type_humanoid_monstrous;
nw::Constant racial_type_humanoid_orc;
nw::Constant racial_type_humanoid_reptilian;
nw::Constant racial_type_elemental;
nw::Constant racial_type_fey;
nw::Constant racial_type_giant;
nw::Constant racial_type_magical_beast;
nw::Constant racial_type_outsider;
nw::Constant racial_type_shapechanger;
nw::Constant racial_type_undead;
nw::Constant racial_type_vermin;
nw::Constant racial_type_all;
nw::Constant racial_type_invalid;
nw::Constant racial_type_ooze;

nw::Constant skill_animal_empathy;
nw::Constant skill_concentration;
nw::Constant skill_disable_trap;
nw::Constant skill_discipline;
nw::Constant skill_heal;
nw::Constant skill_hide;
nw::Constant skill_listen;
nw::Constant skill_lore;
nw::Constant skill_move_silently;
nw::Constant skill_open_lock;
nw::Constant skill_parry;
nw::Constant skill_perform;
nw::Constant skill_persuade;
nw::Constant skill_pick_pocket;
nw::Constant skill_search;
nw::Constant skill_set_trap;
nw::Constant skill_spellcraft;
nw::Constant skill_spot;
nw::Constant skill_taunt;
nw::Constant skill_use_magic_device;
nw::Constant skill_appraise;
nw::Constant skill_tumble;
nw::Constant skill_craft_trap;
nw::Constant skill_bluff;
nw::Constant skill_intimidate;
nw::Constant skill_craft_armor;
nw::Constant skill_craft_weapon;
nw::Constant skill_ride;

bool Profile::load_constants() const
{
    auto* cr = nw::kernel::world().get_mut<nw::ConstantRegistry>();
    if (!cr) { return false; }

    ability_strength = cr->add("ABILITY_STRENGTH", 0);
    ability_dexterity = cr->add("ABILITY_DEXTERITY", 1);
    ability_constitution = cr->add("ABILITY_CONSTITUTION", 2);
    ability_intelligence = cr->add("ABILITY_INTELLIGENCE", 3);
    ability_wisdom = cr->add("ABILITY_WISDOM", 4);
    ability_charisma = cr->add("ABILITY_CHARISMA", 5);

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

    return true;
}

bool Profile::load_compontents() const
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

static nw::RuleValue selector(const nw::Selector& selector, const flecs::entity ent)
{
    switch (selector.type) {
    default:
        return {};
    case nw::SelectorType::ability: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - ability: inavlid subtype");
            return {};
        }
        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) { return {}; }
        auto idx = static_cast<size_t>(selector.subtype.as<int32_t>());
        return static_cast<int>(stats->abilities[idx]);
    }
    case nw::SelectorType::alignment: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - alignment: inavlid subtype");
            return {};
        }
        auto cre = ent.get<nw::Creature>();
        if (!cre) { return {}; }
        if (selector.subtype.as<int32_t>() == 0x1) {
            return cre->lawful_chaotic;
        } else if (selector.subtype.as<int32_t>() == 0x2) {
            return cre->good_evil;
        } else {
            return -1;
        }
    }
    case nw::SelectorType::class_level: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - class_level: inavlid subtype");
            return {};
        }

        auto stats = ent.get<nw::LevelStats>();
        if (!stats) { return 0; }
        for (const auto& ce : stats->classes) {
            if (ce.id == selector.subtype.as<int32_t>()) {
                return ce.level;
            }
        }
        return 0;
    }
    case nw::SelectorType::feat: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - feat: inavlid subtype");
            return {};
        }

        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) { return 0; }
        return stats->has_feat(static_cast<uint16_t>(selector.subtype.as<int32_t>()));
    }
    case nw::SelectorType::level: {
        auto levels = ent.get<nw::LevelStats>();
        if (!levels) { return 0; }

        int level = 0;
        for (const auto& ce : levels->classes) {
            level += ce.level;
        }
        return level;
    }
    case nw::SelectorType::race: {
        auto c = ent.get<nw::Creature>();
        if (!c) { return {}; }
        return static_cast<int>(c->race);
    }
    case nw::SelectorType::skill: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - skill: inavlid subtype");
            return {};
        }

        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) { return {}; }
        auto idx = static_cast<size_t>(selector.subtype.as<int32_t>());
        return static_cast<int>(stats->skills[idx]);
    }
    }

    return {};
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
#define TDA_GET_CONSTANT(tda, name, row, column)                                                              \
    do {                                                                                                      \
        if (tda.get_to(row, column, temp_string)) { name = consts->add(temp_string, static_cast<int>(row)); } \
    } while (0)

/// @private
#define TDA_GET_STRING(tda, name, row, column)                                       \
    do {                                                                             \
        if (tda.get_to(row, column, temp_string)) { name = std::move(temp_string); } \
    } while (0)

bool Profile::load_rules() const
{
    // == Set Selector ========================================================
    nw::kernel::rules().set_selector(selector);

    // == Load Info ===========================================================

    auto* consts = nw::kernel::world().get_mut<nw::ConstantRegistry>();
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
    ability_array->abiliites.reserve(6);
    ability_array->abiliites.push_back({135, ability_strength});
    ability_array->abiliites.push_back({133, ability_dexterity});
    ability_array->abiliites.push_back({132, ability_constitution});
    ability_array->abiliites.push_back({134, ability_intelligence});
    ability_array->abiliites.push_back({136, ability_wisdom});
    ability_array->abiliites.push_back({131, ability_charisma});

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
            // RangedWeapon
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

            baseitem_array->baseitems.push_back(std::move(info));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'baseitems.2da'");
    }

    // Classes
    auto* class_array = nw::kernel::world().get_mut<nw::ClassArray>();
    if (class_array && classes.is_valid()) {
        class_array->classes.reserve(classes.rows());
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
                TDA_GET_CONSTANT(classes, info.constant, i, "Constant");
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
            class_array->classes.push_back(info);
        }

    } else {
        throw std::runtime_error("rules: failed to load 'classes.2da'");
    }

    // Feats
    auto* feat_array = nw::kernel::world().get_mut<nw::FeatArray>();
    if (feat_array && feat.is_valid()) {
        feat_array->feats.reserve(feat.rows());
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
            TDA_GET_CONSTANT(feat, info.constant, i, "Constant");
            TDA_GET_INT(feat, info.tools_categories, i, "TOOLSCATEGORIES");
            TDA_GET_BOOL(feat, info.hostile, i, "HostileFeat");
            TDA_GET_BOOL(feat, info.epic, i, "PreReqEpic");
            TDA_GET_BOOL(feat, info.requires_action, i, "ReqAction");

            feat_array->feats.push_back(std::move(info));
        }
    } else {
        throw std::runtime_error("rules: failed to load 'feat.2da'");
    }

    // Races
    auto* race_array = nw::kernel::world().get_mut<nw::RaceArray>();
    if (race_array && racialtypes.is_valid()) {
        race_array->races.reserve(racialtypes.rows());
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
                TDA_GET_CONSTANT(racialtypes, info.constant, i, "Constant");
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

            race_array->races.push_back(info);
        }
    } else {
        throw std::runtime_error("rules: failed to load 'racialtypes.2da'");
    }

    // Skills
    auto* skill_array = nw::kernel::world().get_mut<nw::SkillArray>();
    if (skill_array && skills.is_valid()) {
        skill_array->skills.reserve(skills.rows());

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
                TDA_GET_CONSTANT(skills, info.constant, i, "Constant");
                TDA_GET_BOOL(skills, info.hostile, i, "HostileSkill");
            }

            skill_array->skills.push_back(info);
        }
    } else {
        throw std::runtime_error("rules: failed to load 'skills.2da'");
    }

    // Spells
    auto* spell_array = nw::kernel::world().get_mut<nw::SpellArray>();
    if (spell_array && spells.is_valid()) {
        spell_array->spells.reserve(spells.rows());
        for (size_t i = 0; i < spells.rows(); ++i) {
            nw::Spell info;
            // float temp_float = 0.0f;

            if (spells.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(spells, info.name, i, "Name");
                TDA_GET_RES(spells, info.icon, i, "IconResRef", nw::ResourceType::texture);
            }

            spell_array->spells.push_back(info);
        }
    } else {
        throw std::runtime_error("rules: failed to load 'spells.2da'");
    }

    // == Process Requirements ================================================

    // BaseItems
    for (size_t i = 0; i < baseitem_array->baseitems.size(); ++i) {
        nw::BaseItem& info = baseitem_array->baseitems[i];
        if (baseitems.get_to(i, "ReqFeat0", temp_int)) {
            info.feat_requirement.add(nw::qualifier::feat(
                feat_array->feats[static_cast<size_t>(temp_int)].constant));
        }

        if (baseitems.get_to(i, "ReqFeat1", temp_int)) {
            info.feat_requirement.add(nw::qualifier::feat(
                feat_array->feats[static_cast<size_t>(temp_int)].constant));
        }

        if (baseitems.get_to(i, "ReqFeat2", temp_int)) {
            info.feat_requirement.add(nw::qualifier::feat(
                feat_array->feats[static_cast<size_t>(temp_int)].constant));
        }

        if (baseitems.get_to(i, "ReqFeat3", temp_int)) {
            info.feat_requirement.add(nw::qualifier::feat(
                feat_array->feats[static_cast<size_t>(temp_int)].constant));
        }

        if (baseitems.get_to(i, "ReqFeat4", temp_int)) {
            info.feat_requirement.add(nw::qualifier::feat(
                feat_array->feats[static_cast<size_t>(temp_int)].constant));
        }
    }

    // Classes

    // Feats

    for (size_t i = 0; i < feat_array->feats.size(); ++i) {
        nw::Feat& info = feat_array->feats[i];
        if (!info.valid()) { continue; }

        if (feat.get_to(i, "MaxLevel", temp_int)) {
            info.requirements.add(nw::qualifier::level(0, temp_int));
        }

        if (feat.get_to(i, "MINSTR", temp_int)) {
            info.requirements.add(nw::qualifier::ability(
                ability_array->abiliites[0].constant, temp_int));
        }

        if (feat.get_to(i, "MINDEX", temp_int)) {
            info.requirements.add(nw::qualifier::ability(
                ability_array->abiliites[1].constant, temp_int));
        }

        if (feat.get_to(i, "MINCON", temp_int)) {
            info.requirements.add(nw::qualifier::ability(
                ability_array->abiliites[2].constant, temp_int));
        }

        if (feat.get_to(i, "MININT", temp_int)) {
            info.requirements.add(nw::qualifier::ability(
                ability_array->abiliites[3].constant, temp_int));
        }

        if (feat.get_to(i, "MINWIS", temp_int)) {
            info.requirements.add(nw::qualifier::ability(
                ability_array->abiliites[4].constant, temp_int));
        }

        if (feat.get_to(i, "MINCHA", temp_int)) {
            info.requirements.add(nw::qualifier::ability(
                ability_array->abiliites[5].constant, temp_int));
        }

        if (feat.get_to(i, "PREREQFEAT1", temp_int)) {
            info.requirements.add(nw::qualifier::feat(
                feat_array->feats[static_cast<size_t>(temp_int)].constant));
        }

        if (feat.get_to(i, "PREREQFEAT2", temp_int)) {
            info.requirements.add(nw::qualifier::feat(
                feat_array->feats[static_cast<size_t>(temp_int)].constant));
        }
    }

    // Races

    // Skills

    // Spells

    return true;
}

#undef TDA_GET_STRING
#undef TDA_GET_STRREF
#undef TDA_GET_FLOAT
#undef TDA_GET_INT
#undef TDA_GET_BOOL
#undef TDA_GET_RES

} // namespace nwn1
