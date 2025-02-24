#include "attributes.hpp"

#include "../formats/StaticTwoDA.hpp"
#include "../kernel/Strings.hpp"

#include "nlohmann/json.hpp"

namespace nw {

DEFINE_RULE_TYPE(Ability);
DEFINE_RULE_TYPE(Appearance);
DEFINE_RULE_TYPE(ArmorClass);
DEFINE_RULE_TYPE(Phenotype);
DEFINE_RULE_TYPE(Race);
DEFINE_RULE_TYPE(Save);
DEFINE_RULE_TYPE(SaveVersus);
DEFINE_RULE_TYPE(Skill);

// -- Alignment ---------------------------------------------------------------
// ----------------------------------------------------------------------------

AlignmentAxis alignment_axis_from_flags(AlignmentFlags flags)
{
    static constexpr AlignmentFlags good_evil{AlignmentFlags::good | AlignmentFlags::evil};
    static constexpr AlignmentFlags law_chaos{AlignmentFlags::lawful | AlignmentFlags::chaotic};

    AlignmentAxis result = AlignmentAxis::neither;

    if (to_bool(flags | good_evil)) {
        result |= AlignmentAxis::good_evil;
    }

    if (to_bool(flags | law_chaos)) {
        result |= AlignmentAxis::law_chaos;
    }

    return result;
}

// -- AppearanceInfo ----------------------------------------------------------
// ----------------------------------------------------------------------------

AppearanceInfo::AppearanceInfo(const TwoDARowView& tda)
{
    tda.get_to("LABEL", label);
    tda.get_to("STRING_REF", string_ref);
    tda.get_to("NAME", base_name);
    tda.get_to("RACE", model_name);
    tda.get_to("SIZECATEGORY", size);

    tda.get_to("MODELTYPE", model_type);

    std::string temp;
    tda.get_to("MOVERATE", temp);
    if (string::icmp(temp, "PLAYER")) {
        walkrate = 0;
    } else if (string::icmp(temp, "NOMOVE")) {
        walkrate = 1;
    } else if (string::icmp(temp, "VSLOW")) {
        walkrate = 2;
    } else if (string::icmp(temp, "SLOW")) {
        walkrate = 3;
    } else if (string::icmp(temp, "NORM")) {
        walkrate = 4;
    } else if (string::icmp(temp, "FAST")) {
        walkrate = 5;
    } else if (string::icmp(temp, "VFAST")) {
        walkrate = 6;
    } else if (string::icmp(temp, "DEFAULT")) {
        walkrate = 7;
    } else if (string::icmp(temp, "DFAST")) {
        walkrate = 8;
    }
}

String AppearanceInfo::editor_name() const
{
    auto string = nw::kernel::strings().get(string_ref);
    if (!string.empty()) { return string; }
    return label;
}

// -- PhenotypeInfo -----------------------------------------------------------
// ----------------------------------------------------------------------------

PhenotypeInfo::PhenotypeInfo(const TwoDARowView& tda)
{
    tda.get_to("Name", name);
    tda.get_to("DefaultPhenoType", fallback);
}

String PhenotypeInfo::editor_name() const
{
    return nw::kernel::strings().get(name);
}

// -- Race --------------------------------------------------------------------
// ----------------------------------------------------------------------------

RaceInfo::RaceInfo(const TwoDARowView& tda)
{
    String temp_string;
    if (tda.get_to("label", temp_string)) {
        tda.get_to("Name", name);
        tda.get_to("ConverName", name_conversation);
        tda.get_to("ConverNameLower", name_conversation_lower);
        tda.get_to("NamePlural", name_plural);
        tda.get_to("Description", description);
        if (tda.get_to("Icon", temp_string)) {
            icon = {temp_string, nw::ResourceType::texture};
        }
        tda.get_to("appearance", appearance);
        tda.get_to("StrAdjust", ability_modifiers[0]);
        tda.get_to("DexAdjust", ability_modifiers[1]);
        tda.get_to("ConAdjust", ability_modifiers[2]);
        tda.get_to("IntAdjust", ability_modifiers[3]);
        tda.get_to("WisAdjust", ability_modifiers[4]);
        tda.get_to("ChaAdjust", ability_modifiers[5]);
        tda.get_to("Favored", favored_class);
        if (tda.get_to("FeatsTable", temp_string)) {
            feats_table = {temp_string, nw::ResourceType::twoda};
        }
        tda.get_to("Biography", biography);
        tda.get_to("PlayerRace", player_race);
        if (tda.get_to("Constant", temp_string)) {
            constant = nw::kernel::strings().intern(temp_string);
        }
        tda.get_to("age", age);
        tda.get_to("CRModifier", cr_modifier);
        tda.get_to("ExtraFeatsAtFirstLevel", feats_extra_1st_level);
        tda.get_to("ExtraSkillPointsPerLevel", skillpoints_extra_per_level);
        tda.get_to("FirstLevelSkillPointsMultiplier", skillpoints_1st_level_multiplier);
        tda.get_to("AbilitiesPointBuyNumber", ability_point_buy_number);
        tda.get_to("NormalFeatEveryNthLevel", feats_normal_level);
        tda.get_to("NumberNormalFeatsEveryNthLevel", feats_normal_amount);
        tda.get_to("SkillPointModifierAbility", skillpoints_ability);
    }
}

String RaceInfo::editor_name() const
{
    return nw::kernel::strings().get(name);
}

// -- Skill -------------------------------------------------------------------
// ----------------------------------------------------------------------------

SkillInfo::SkillInfo(const TwoDARowView& tda)
{
    String temp_string;
    if (tda.get_to("label", temp_string)) {
        tda.get_to("Name", name);
        tda.get_to("Description", description);
        if (tda.get_to("Icon", temp_string)) {
            icon = {temp_string, nw::ResourceType::texture};
        }
        tda.get_to("Untrained", untrained);

        tda.get_to("ArmorCheckPenalty", armor_check_penalty);
        tda.get_to("AllClassesCanUse", all_can_use);
        if (tda.get_to("Constant", temp_string)) {
            constant = nw::kernel::strings().intern(temp_string);
        }
        tda.get_to("HostileSkill", hostile);
    }
}

String SkillInfo::editor_name() const
{
    return nw::kernel::strings().get(name);
}
} // namespace nw
