#include "Rules.hpp"

#include "../formats/TwoDA.hpp"
#include "../util/string.hpp"
#include "Kernel.hpp"

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    ability_info_.clear();
    baseitem_info_.clear();
    class_info_.clear();
    feat_info_.clear();
    race_info_.clear();
    skill_info_.clear();
    spell_info_.clear();
    cached_2das_.clear();
    constants_.clear();
}

bool Rules::initialize()
{
    // Stub
    return true;
}

#define TDA_GET_UINT32(name, row, column)           \
    do {                                            \
        if (tda.get_to(row, column, temp_int)) {    \
            name = static_cast<uint32_t>(temp_int); \
        }                                           \
    } while (0)

#define TDA_GET_FLOAT(name, row, column)                                \
    do {                                                                \
        if (tda.get_to(row, column, temp_float)) { name = temp_float; } \
    } while (0)

#define TDA_GET_INT(name, row, column)                              \
    do {                                                            \
        if (tda.get_to(row, column, temp_int)) { name = temp_int; } \
    } while (0)

#define TDA_GET_BOOL(name, row, column)                               \
    do {                                                              \
        if (tda.get_to(row, column, temp_int)) { name = !!temp_int; } \
    } while (0)

#define TDA_GET_RES(name, row, column, type)                                              \
    do {                                                                                  \
        if (tda.get_to(row, column, temp_string)) { name = Resource{temp_string, type}; } \
    } while (0)

#define TDA_GET_CONSTANT(name, row, column)                                                                             \
    do {                                                                                                                \
        if (tda.get_to(row, column, temp_string)) { name = register_constant(temp_string, static_cast<int32_t>(row)); } \
    } while (0)

#define TDA_GET_STRING(name, row, column)                                            \
    do {                                                                             \
        if (tda.get_to(row, column, temp_string)) { name = std::move(temp_string); } \
    } while (0)

bool Rules::load(bool fail_hard)
{
    LOG_F(INFO, "kernel: initializing rules system");

    load_abilities(fail_hard);
    load_skills(fail_hard);
    load_feats(fail_hard);
    load_spells(fail_hard);
    load_baseitems(fail_hard);
    load_classes(fail_hard);
    load_races(fail_hard);

    return true;
}

void Rules::load_abilities(bool fail_hard)
{
    LIBNW_UNUSED(fail_hard);

    // Abilities - Going to have to cheat on this for now..
    ability_info_.reserve(6);
    ability_info_.push_back({135, register_constant("ABILITY_STRENGTH", 0)});
    ability_info_.push_back({133, register_constant("ABILITY_DEXTERITY", 1)});
    ability_info_.push_back({132, register_constant("ABILITY_CONSTITUTION", 2)});
    ability_info_.push_back({134, register_constant("ABILITY_INTELLIGENCE", 3)});
    ability_info_.push_back({136, register_constant("ABILITY_WISDOM", 4)});
    ability_info_.push_back({131, register_constant("ABILITY_CHARISMA", 5)});
}

void Rules::load_baseitems(bool fail_hard)
{
    TwoDA tda{resman().demand({"baseitems"sv, ResourceType::twoda})};
    std::string temp_string;
    if (tda.is_valid()) {
        for (size_t i = 0; i < tda.rows(); ++i) {
            if (!tda.get_to(0, "label", temp_string)) { continue; }
            int temp_int = 0;
            BaseItem info;
            TDA_GET_UINT32(info.name, i, "Name");
            TDA_GET_INT(info.inventory_slot_size.first, i, "InvSlotWidth");
            TDA_GET_INT(info.inventory_slot_size.second, i, "InvSlotHeight");
            TDA_GET_UINT32(info.equipable_slots, i, "EquipableSlots");
            TDA_GET_BOOL(info.can_rotate_icon, i, "CanRotateIcon");
            // ModelType
            // ItemClass
            TDA_GET_BOOL(info.gender_specific, i, "GenderSpecific");
            TDA_GET_BOOL(std::get<0>(info.composite_env_map), i, "Part1EnvMap");
            TDA_GET_BOOL(std::get<1>(info.composite_env_map), i, "Part2EnvMap");
            TDA_GET_BOOL(std::get<2>(info.composite_env_map), i, "Part3EnvMap");
            TDA_GET_RES(info.default_model, i, "DefaultModel", ResourceType::mdl);
            TDA_GET_STRING(info.default_icon, i, "DefaultIcon");
            TDA_GET_BOOL(info.is_container, i, "Container");
            // WeaponWield
            // WeaponType
            // WeaponSize
            // RangedWeapon
            // PrefAttackDist
            // MinRange
            // MaxRange
            // NumDice
            // DieToRoll
            // CritThreat
            // CritHitMult
            // Category
            // BaseCost
            // Stacking
            // ItemMultiplier
            TDA_GET_UINT32(info.description, i, "Description");
            // InvSoundType
            // MaxProps
            // MinProps
            // PropColumn
            // StorePanel
            // ReqFeat0, ReqFeat1, ReqFeat2, ReqFeat3, ReqFeat4
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
            // WeaponFocusFeat
            // EpicWeaponFocusFeat
            // WeaponSpecializationFeat
            // EpicWeaponSpecializationFeat
            // WeaponImprovedCriticalFeat
            // EpicWeaponOverwhelmingCriticalFeat
            // EpicWeaponDevastatingCriticalFeat
            // WeaponOfChoiceFeat
            TDA_GET_BOOL(info.is_monk_weapon, i, "IsMonkWeapon");
            // WeaponFinesseMinimumCreatureSize
            baseitem_info_.push_back(std::move(info));
        }
    } else if (fail_hard) {
        throw std::runtime_error("rules: failed to load 'basetimes.2da'");
    } else {
        LOG_F(WARNING, "rules: failed to load 'basetimes.2da'");
    }
}

void Rules::load_classes(bool fail_hard)
{
    TwoDA tda{resman().demand({"classes"sv, ResourceType::twoda})};
    std::string temp_string;

    if (tda.is_valid()) {
        for (size_t i = 0; i < tda.rows(); ++i) {
            Class info;
            int temp_int = 0;
            float temp_float = 0.0f;

            if (tda.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(info.name, i, "Name");
                TDA_GET_UINT32(info.plural, i, "Plural");
                TDA_GET_UINT32(info.lower, i, "Lower");
                TDA_GET_UINT32(info.description, i, "Description");
                TDA_GET_RES(info.icon, i, "Icon", ResourceType::texture);
                TDA_GET_INT(info.hitdie, i, "HitDie");
                TDA_GET_RES(info.attack_bonus_table, i, "AttackBonusTable", ResourceType::twoda);
                TDA_GET_RES(info.feats_table, i, "FeatsTable", ResourceType::twoda);
                TDA_GET_RES(info.saving_throw_table, i, "SavingThrowTable", ResourceType::twoda);
                TDA_GET_RES(info.skills_table, i, "SkillsTable", ResourceType::twoda);
                TDA_GET_RES(info.bonus_feats_table, i, "BonusFeatsTable", ResourceType::twoda);
                TDA_GET_INT(info.skill_point_base, i, "SkillPointBase");
                TDA_GET_RES(info.spell_gain_table, i, "SpellGainTable", ResourceType::twoda);
                TDA_GET_RES(info.spell_known_table, i, "SpellKnownTable", ResourceType::twoda);
                TDA_GET_BOOL(info.player_class, i, "PlayerClass");
                TDA_GET_BOOL(info.spellcaster, i, "SpellCaster");

                TDA_GET_INT(info.primary_ability, i, "PrimaryAbil");
                TDA_GET_UINT32(info.alignment_restriction, i, "AlignRestrict");
                TDA_GET_UINT32(info.alignment_rstrction_type, i, "AlignRstrctType");
                TDA_GET_BOOL(info.invert_restriction, i, "InvertRestrict");
                TDA_GET_CONSTANT(info.constant, i, "Constant");
                TDA_GET_RES(info.prereq_table, i, "PreReqTable", ResourceType::twoda);
                TDA_GET_INT(info.max_level, i, "MaxLevel");
                TDA_GET_INT(info.xp_penalty, i, "XPPenalty");

                if (!info.spellcaster) {
                    TDA_GET_INT(info.arcane_spellgain_mod, i, "ArcSpellLvlMod");
                    TDA_GET_INT(info.divine_spellgain_mod, i, "DivSpellLvlMod");
                }

                TDA_GET_INT(info.epic_level_limit, i, "EpicLevel");
                TDA_GET_INT(info.package, i, "Package");
                TDA_GET_RES(info.stat_gain_table, i, "StatGainTable", ResourceType::twoda);

                // No point in checking for anyone else
                if (info.spellcaster) {
                    TDA_GET_BOOL(info.memorizes_spells, i, "MemorizesSpells");
                    TDA_GET_BOOL(info.spellbook_restricted, i, "SpellbookRestricted");
                    TDA_GET_BOOL(info.pick_domains, i, "PickDomains");
                    TDA_GET_BOOL(info.pick_school, i, "PickSchool");
                    TDA_GET_BOOL(info.learn_scroll, i, "LearnScroll");
                    TDA_GET_BOOL(info.arcane, i, "Arcane");
                    TDA_GET_BOOL(info.arcane_spell_failure, i, "ASF");
                    TDA_GET_INT(info.caster_ability, i, "SpellcastingAbil");
                    TDA_GET_STRING(info.spell_table_column, i, "SpellTableColumn");
                    TDA_GET_FLOAT(info.caster_level_multiplier, i, "CLMultiplier");
                    TDA_GET_INT(info.level_min_caster, i, "MinCastingLevel");
                    TDA_GET_INT(info.level_min_associate, i, "MinAssociateLevel");
                }

                if (info.spellcaster) {
                    TDA_GET_BOOL(info.can_cast_spontaneously, i, "CanCastSpontaneously");
                }
            }
            class_info_.push_back(info);
        }

    } else if (fail_hard) {
        throw std::runtime_error("rules: failed to load 'classes.2da'");
    } else {
        LOG_F(WARNING, "rules: failed to load 'classes.2da'");
    }
}

void Rules::load_feats(bool fail_hard)
{
    TwoDA tda{resman().demand({"feat"sv, ResourceType::twoda})};
    std::string temp_string;

    if (tda.is_valid()) {
        feat_info_.reserve(tda.rows());
        for (size_t i = 0; i < tda.rows(); ++i) {
            Feat info;
            int temp_int = 0;
            float temp_float = 0.0f;

            if (!tda.get_to(0, "label", temp_string)) { continue; }

            TDA_GET_UINT32(info.name, i, "FEAT");
            TDA_GET_UINT32(info.description, i, "DESCRIPTION");
            TDA_GET_RES(info.icon, i, "ICON", ResourceType::texture);
            TDA_GET_BOOL(info.all_can_use, i, "ALLCLASSESCANUSE");
            TDA_GET_INT(info.category, i, "CATEGORY");
            TDA_GET_INT(info.max_cr, i, "MAXCR");
            TDA_GET_INT(info.spell, i, "SPELLID");
            TDA_GET_UINT32(info.successor, i, "SUCCESSOR");
            TDA_GET_FLOAT(info.cr_value, i, "CRValue");
            TDA_GET_INT(info.uses, i, "USESPERDAY");
            TDA_GET_INT(info.master, i, "MASTERFEAT");
            TDA_GET_BOOL(info.target_self, i, "TARGETSELF");
            TDA_GET_CONSTANT(info.constant, i, "Constant");
            TDA_GET_INT(info.tools_categories, i, "TOOLSCATEGORIES");
            TDA_GET_BOOL(info.hostile, i, "HostileFeat");
            TDA_GET_BOOL(info.epic, i, "PreReqEpic");
            TDA_GET_BOOL(info.requires_action, i, "ReqAction");

            if (tda.get_to(i, "MaxLevel", temp_int)) {
                info.requirements.add(qualifier::level(0, temp_int));
            }

            feat_info_.push_back(std::move(info));
        }
    } else if (fail_hard) {
        throw std::runtime_error("rules: failed to load 'feat.2da'");
    } else {
        LOG_F(WARNING, "rules: failed to load 'feat.2da'");
    }
}

void Rules::load_races(bool fail_hard)
{
    TwoDA tda{resman().demand({"racialtypes"sv, ResourceType::twoda})};
    std::string temp_string;

    if (tda.is_valid()) {
        race_info_.reserve(tda.rows());
        for (size_t i = 0; i < tda.rows(); ++i) {
            Race info;
            int temp_int = 0;
            float temp_float = 0.0f;

            if (tda.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(info.name, i, "Name");
                TDA_GET_UINT32(info.name_conversation, i, "ConverName");
                TDA_GET_UINT32(info.name_conversation_lower, i, "ConverNameLower");
                TDA_GET_UINT32(info.name_plural, i, "NamePlural");
                TDA_GET_UINT32(info.description, i, "Description");
                TDA_GET_RES(info.icon, i, "Icon", ResourceType::texture);
                TDA_GET_INT(info.appearance, i, "appearance");
                TDA_GET_INT(info.ability_modifiers[0], i, "StrAdjust");
                TDA_GET_INT(info.ability_modifiers[1], i, "DexAdjust");
                TDA_GET_INT(info.ability_modifiers[2], i, "ConAdjust");
                TDA_GET_INT(info.ability_modifiers[3], i, "IntAdjust");
                TDA_GET_INT(info.ability_modifiers[4], i, "WisAdjust");
                TDA_GET_INT(info.ability_modifiers[5], i, "ChaAdjust");
                TDA_GET_INT(info.favored_class, i, "Favored");
                TDA_GET_RES(info.feats_table, i, "FeatsTable", ResourceType::twoda);
                TDA_GET_UINT32(info.biography, i, "Biography");
                TDA_GET_BOOL(info.player_race, i, "PlayerRace");
                TDA_GET_CONSTANT(info.constant, i, "Constant");
                TDA_GET_INT(info.age, i, "age");
                TDA_GET_FLOAT(info.cr_modifier, i, "CRModifier");
                TDA_GET_INT(info.feats_extra_1st_level, i, "ExtraFeatsAtFirstLevel");
                TDA_GET_INT(info.skillpoints_extra_per_level, i, "ExtraSkillPointsPerLevel");
                TDA_GET_INT(info.skillpoints_1st_level_multiplier, i, "FirstLevelSkillPointsMultiplier");
                TDA_GET_INT(info.ability_point_buy_number, i, "AbilitiesPointBuyNumber");
                TDA_GET_INT(info.feats_normal_level, i, "NormalFeatEveryNthLevel");
                TDA_GET_INT(info.feats_normal_amount, i, "NumberNormalFeatsEveryNthLevel");
                TDA_GET_INT(info.skillpoints_ability, i, "SkillPointModifierAbility");
            }

            race_info_.push_back(info);
        }
    } else if (fail_hard) {
        throw std::runtime_error("rules: failed to load 'racialtypes.2da'");
    } else {
        LOG_F(WARNING, "rules: failed to load 'racialtypes.2da'");
    }
}

void Rules::load_skills(bool fail_hard)
{
    // Skills
    TwoDA tda{resman().demand({"skills"sv, ResourceType::twoda})};
    std::string temp_string;
    int temp_int = 0;

    if (tda.is_valid()) {
        skill_info_.reserve(tda.rows());
        for (size_t i = 0; i < tda.rows(); ++i) {
            Skill info;

            if (tda.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(info.name, i, "Name");
                TDA_GET_UINT32(info.description, i, "Description");
                TDA_GET_RES(info.icon, i, "Icon", ResourceType::texture);
                TDA_GET_BOOL(info.untrained, i, "Untrained");

                if (tda.get_to(i, "KeyAbility", temp_string)) {
                    if (string::icmp("str", temp_string)) {
                        info.ability = get_constant("ABILITY_STRENGTH");
                    } else if (string::icmp("dex", temp_string)) {
                        info.ability = get_constant("ABILITY_DEXTERITY");
                    } else if (string::icmp("con", temp_string)) {
                        info.ability = get_constant("ABILITY_CONSTITUTION");
                    } else if (string::icmp("int", temp_string)) {
                        info.ability = get_constant("ABILITY_INTELLIGENCE");
                    } else if (string::icmp("wis", temp_string)) {
                        info.ability = get_constant("ABILITY_WISDOM");
                    } else if (string::icmp("cha", temp_string)) {
                        info.ability = get_constant("ABILITY_CHARISMA");
                    } else {
                        // Try to get whatever is there..
                        info.ability = get_constant(temp_string);
                    }
                }
                // KeyAbility
                TDA_GET_BOOL(info.armor_check_penalty, i, "ArmorCheckPenalty");
                TDA_GET_BOOL(info.all_can_use, i, "AllClassesCanUse");
                TDA_GET_CONSTANT(info.constant, i, "Constant");
                TDA_GET_BOOL(info.hostile, i, "HostileSkill");
            }

            skill_info_.push_back(info);
        }
    } else if (fail_hard) {
        throw std::runtime_error("rules: failed to load 'skills.2da'");
    } else {
        LOG_F(WARNING, "rules: failed to load 'skills.2da'");
    }
}

void Rules::load_spells(bool fail_hard)
{
    TwoDA tda{resman().demand({"spells"sv, ResourceType::twoda})};
    std::string temp_string;

    if (tda.is_valid()) {
        skill_info_.reserve(tda.rows());
        for (size_t i = 0; i < tda.rows(); ++i) {
            Spell info;
            int temp_int = 0;
            // float temp_float = 0.0f;

            if (tda.get_to(0, "label", temp_string)) {
                TDA_GET_UINT32(info.name, i, "Name");
                TDA_GET_RES(info.icon, i, "IconResRef", ResourceType::texture);
            }

            spell_info_.push_back(info);
        }
    } else if (fail_hard) {
        throw std::runtime_error("rules: failed to load 'spells.2da'");
    } else {
        LOG_F(WARNING, "rules: failed to load 'spells.2da'");
    }
}

#undef TDA_GET_STRING
#undef TDA_GET_STRREF
#undef TDA_GET_FLOAT
#undef TDA_GET_INT
#undef TDA_GET_BOOL
#undef TDA_GET_RES

bool Rules::reload(bool fail_hard)
{
    clear();
    return load(fail_hard);
}

bool Rules::cache_2da(const Resource& res)
{
    auto it = cached_2das_.find(res);
    if (it != std::end(cached_2das_) && it->second.is_valid() && it->second.rows() != 0) {
        return true;
    }

    auto tda = TwoDA{resman().demand(res)};
    if (tda.is_valid()) {
        cached_2das_[res] = std::move(tda);
        return true;
    }
    return false;
}

TwoDA& Rules::get_cached_2da(const Resource& res)
{
    return cached_2das_[res];
}

Constant Rules::get_constant(std::string_view name) const
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return Constant{it->first, it->second};
    }

    return {};
}

const Ability& Rules::ability(size_t index) const
{
    return ability_info_.at(index);
}

size_t Rules::ability_count() const noexcept
{
    return ability_info_.size();
}

const BaseItem& Rules::baseitem(size_t index)
{
    return baseitem_info_.at(index);
}

size_t Rules::baseitem_count() const noexcept
{
    return baseitem_info_.size();
}

const Class& Rules::classes(size_t index)
{
    return class_info_.at(index);
}

size_t Rules::class_count() const noexcept
{
    return class_info_.size();
}

const Feat& Rules::feat(size_t index)
{
    return feat_info_.at(index);
}

size_t Rules::feat_count() const noexcept
{
    return feat_info_.size();
}

const Race& Rules::race(size_t index)
{
    return race_info_.at(index);
}

size_t Rules::race_count() const noexcept
{
    return race_info_.size();
}

Constant Rules::register_constant(std::string_view name, RuleBaseVariant value)
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return Constant{it->first, it->second};
    }

    auto intstr = kernel::strings().intern(name);
    constants_.emplace(intstr, value);

    return {intstr, value};
}

const Skill& Rules::skill(size_t index)
{
    return skill_info_.at(index);
}

size_t Rules::skill_count() const noexcept
{
    return skill_info_.size();
}

const Spell& Rules::spell(size_t index)
{
    return spell_info_.at(index);
}

size_t Rules::spell_count() const noexcept
{
    return spell_info_.size();
}

} // namespace nw::kernel
