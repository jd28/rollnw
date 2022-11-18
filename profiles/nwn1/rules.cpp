#include "rules.hpp"

#include "constants.hpp"
#include "constants/const_feat.hpp"
#include "functions.hpp"
#include "functions/funcs_feat.hpp"
#include "nw/rules/combat.hpp"
#include "nw/rules/items.hpp"

#include <nw/components/Common.hpp>
#include <nw/components/Creature.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/log.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/feats.hpp>

namespace nwn1 {

bool match(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto value = selector(qual.selector, obj);
    if (!value.empty()) {
        switch (qual.selector.type) {
        default:
            return false;
        case nw::SelectorType::alignment: {
            auto target_axis = static_cast<nw::AlignmentAxis>(qual.selector.subtype.as<int32_t>());
            auto flags = static_cast<nw::AlignmentFlags>(qual.params[0].as<int32_t>());
            auto ge = 50;
            auto lc = 50;

            auto ge_sel = selector(sel::alignment(nw::AlignmentAxis::good_evil), obj);
            if (ge_sel.is<int32_t>()) {
                ge = ge_sel.as<int32_t>();
            }

            auto lc_sel = selector(sel::alignment(nw::AlignmentAxis::law_chaos), obj);
            if (lc_sel.is<int32_t>()) {
                lc = lc_sel.as<int32_t>();
            }

            if (!!(flags & nw::AlignmentFlags::good) && !!(target_axis | nw::AlignmentAxis::good_evil)) {
                if (ge > 50) {
                    return true;
                }
            }

            if (!!(flags & nw::AlignmentFlags::evil) && !!(target_axis | nw::AlignmentAxis::good_evil)) {
                if (ge < 50) {
                    return true;
                }
            }

            if (!!(flags & nw::AlignmentFlags::lawful) && !!(target_axis | nw::AlignmentAxis::law_chaos)) {
                if (lc > 50) {
                    return true;
                }
            }

            if (!!(flags & nw::AlignmentFlags::chaotic) && !!(target_axis | nw::AlignmentAxis::law_chaos)) {
                if (lc < 50) {
                    return true;
                }
            }

            if (!!(flags & nw::AlignmentFlags::neutral)) {
                if (target_axis == nw::AlignmentAxis::both) {
                    return ge == 50 && lc == 50;
                }
                if (target_axis == nw::AlignmentAxis::good_evil) {
                    return ge == 50;
                }
                if (target_axis == nw::AlignmentAxis::law_chaos) {
                    return lc == 50;
                }
            }
        } break;
        case nw::SelectorType::class_level:
        case nw::SelectorType::level: {
            auto val = value.as<int32_t>();
            auto min = qual.params[0].as<int32_t>();
            auto max = qual.params[1].as<int32_t>();
            if (val < min || (max != 0 && val > max)) {
                return false;
            }
            return true;
        }
        case nw::SelectorType::feat: {
            return value.is<int32_t>() && value.as<int32_t>();
        }
        case nw::SelectorType::race: {
            auto val = value.as<int32_t>();
            return val == qual.params[0].as<int32_t>();
        }
        case nw::SelectorType::skill:
        case nw::SelectorType::ability: {
            auto val = value.as<int32_t>();
            auto min = qual.params[0].as<int32_t>();
            auto max = qual.params[1].as<int32_t>();
            if (val < min) {
                return false;
            }
            if (max != 0 && val > max) {
                return false;
            }
            return true;
        } break;
        }
    }
    return false;
}

nw::RuleValue selector(const nw::Selector& selector, const nw::ObjectBase* obj)
{
    switch (selector.type) {
    default:
        return {};
    case nw::SelectorType::ability: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - ability: invalid subtype");
            return {};
        }
        return get_ability_score(obj->as_creature(), nw::Ability::make(selector.subtype.as<int32_t>()));
    }
    case nw::SelectorType::alignment: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - alignment: invalid subtype");
            return {};
        }
        auto cre = obj->as_creature();
        if (!cre) {
            return {};
        }
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
            LOG_F(ERROR, "selector - class_level: invalid subtype");
            return {};
        }

        auto cre = obj->as_creature();
        if (!cre) {
            return {};
        }

        for (const auto& ce : cre->levels.entries) {
            if (ce.id == nw::Class::make(selector.subtype.as<int32_t>())) {
                return ce.level;
            }
        }
        return 0;
    }
    case nw::SelectorType::feat: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - feat: invalid subtype");
            return {};
        }

        auto cre = obj->as_creature();
        if (!cre) {
            return {};
        }

        return cre->stats.has_feat(nw::Feat::make(selector.subtype.as<int32_t>()));
    }
    case nw::SelectorType::level: {
        auto cre = obj->as_creature();
        if (!cre) {
            return {};
        }

        int level = 0;
        for (const auto& ce : cre->levels.entries) {
            level += ce.level;
        }
        return level;
    }
    case nw::SelectorType::local_var_int: {
        auto common = obj->as_common();
        if (!selector.subtype.is<std::string>()) {
            LOG_F(ERROR, "selector - local_var_int: invalid subtype");
            return {};
        }
        if (!common) {
            return {};
        }
        return common->locals.get_int(selector.subtype.as<std::string>());
    }
    case nw::SelectorType::local_var_str: {
        auto common = obj->as_common();
        if (!selector.subtype.is<std::string>()) {
            LOG_F(ERROR, "selector - local_var_str: invalid subtype");
            return {};
        }
        if (!common) {
            return {};
        }
        return common->locals.get_string(selector.subtype.as<std::string>());
    }
    case nw::SelectorType::race: {
        auto c = obj->as_creature();
        if (!c) {
            return {};
        }
        return *c->race;
    }
    case nw::SelectorType::skill: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - skill: invalid subtype");
            return {};
        }
        return get_skill_rank(obj->as_creature(), nw::Skill::make(selector.subtype.as<int32_t>()));
    }
    }

    return {};
}

nw::ModifierFunction simple_feat_mod(nw::Feat feat, int value)
{
    return [feat, value](const nw::ObjectBase* obj) {
        auto cre = obj->as_creature();
        if (cre && cre->stats.has_feat(feat)) {
            return value;
        }
        return 0;
    };
}

nw::ModifierResult epic_great_ability(const nw::ObjectBase* obj, int32_t subtype)
{
    if (subtype < 0 || subtype >= 5) return 0;
    nw::Ability abil = nw::Ability::make(subtype);
    nw::Feat start, stop;
    switch (*abil) {
    default:
        return 0;
    case *ability_strength:
        start = feat_epic_great_strength_1;
        stop = feat_epic_great_strength_10;
        break;
    case *ability_dexterity:
        start = feat_epic_great_dexterity_1;
        stop = feat_epic_great_dexterity_10;
        break;
    case *ability_constitution:
        start = feat_epic_great_constitution_1;
        stop = feat_epic_great_constitution_10;
        break;
    case *ability_intelligence:
        start = feat_epic_great_intelligence_1;
        stop = feat_epic_great_intelligence_10;
        break;
    case *ability_wisdom:
        start = feat_epic_great_wisdom_1;
        stop = feat_epic_great_wisdom_10;
        break;
    case *ability_charisma:
        start = feat_epic_great_charisma_1;
        stop = feat_epic_great_charisma_10;
        break;
    }

    auto nth = highest_feat_in_range(obj->as_creature(), start, stop);
    if (nth == nw::Feat::invalid()) {
        return 0;
    }
    return *nth - *start + 1;
}

nw::ModifierResult dragon_disciple_ac(const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();

    if (!obj) {
        return 0;
    }

    auto level = cre->levels.level_by_class(nwn1::class_type_dragon_disciple);

    if (level >= 10) {
        return (level / 5) + 2;
    } else if (level >= 1 && level <= 4) {
        return 1;
    } else if (level >= 5 && level <= 7) {
        return 2;
    } else if (level >= 8) {
        return 3;
    }
    return 0;
}

nw::ModifierResult pale_master_ac(const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    if (!cre) { return 0; }
    auto pm_level = cre->levels.level_by_class(nwn1::class_type_pale_master);

    return pm_level > 0 ? ((pm_level / 4) + 1) * 2 : 0;
}

// Attack Bonus
nw::ModifierResult enchant_arrow_ab(const nw::ObjectBase* obj, int32_t subtype)
{
    auto baseitem = nw::BaseItem::make(subtype);
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    if (baseitem == base_item_longbow || baseitem == base_item_shortbow) {
        auto feat = highest_feat_in_range(cre, feat_prestige_enchant_arrow_6,
            feat_prestige_enchant_arrow_20);
        if (feat != nw::Feat::invalid()) {
            return (*feat - *feat_prestige_enchant_arrow_6) + 6;
        } else {
            feat = highest_feat_in_range(cre, feat_prestige_enchant_arrow_1,
                feat_prestige_enchant_arrow_5);
            if (feat != nw::Feat::invalid()) {
                return (*feat - *feat_prestige_enchant_arrow_1) + 1;
            }
        }
    }
    return 0;
}

nw::ModifierResult target_state_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target)
{
    int result = 0;
    if (!obj || !target) { return result; }
    auto cre = obj->as_creature();
    auto vs = target->as_creature();
    if (!cre || !vs) { return result; }

    if (to_bool(cre->target_state & nw::TargetState::attacker_invis)
        || to_bool(cre->target_state & nw::TargetState::blind)) {
        if (!vs->stats.has_feat(feat_blind_fight)) {
            result += 2;
        }
    }

    if (to_bool(cre->target_state & nw::TargetState::stunned)) {
        result += 2;
    }

    if (to_bool(cre->target_state & nw::TargetState::flanked)) {
        if (!vs->stats.has_feat(feat_prestige_defensive_awareness_2)) {
            result += 2;
        }
    }

    if (to_bool(cre->target_state & nw::TargetState::unseen)
        || to_bool(cre->target_state & nw::TargetState::invis)) {
        result -= 4;
    }

    // Others - will need to wait
    //
    // Melee only
    // Target is prone. +4AB
    // Ranged only
    // Attacker is mounted. -4AB (or -2AB with mounted archery)
    // Target is moving. -2AB
    // Target is prone. -4AB
    // Target is within 3.5 meters of the attacker. -4AB (unless negated by point blank shot)

    return result;
}

nw::ModifierResult weapon_master_ab(const nw::ObjectBase* obj, int32_t subtype)
{
    auto baseitem = nw::BaseItem::make(subtype);
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto wm = cre->levels.level_by_class(nwn1::class_type_weapon_master);
    if (wm < 5) { return 0; }

    bool has_feat = !!nw::kernel::resolve_master_feat<int>(cre, baseitem, mfeat_weapon_of_choice);
    if (!has_feat) { return 0; }

    int result = 1;

    if (wm >= 13) {
        result += (wm - 10) / 3;
    }

    return result;
}

// Damage Resist
nw::ModifierResult energy_resistance(const nw::ObjectBase* obj, int32_t subtype)
{
    if (!obj || !obj->as_creature()) {
        return {};
    }
    auto cre = obj->as_creature();
    auto dmg_type = nw::Damage::make(subtype);
    nw::Feat feat_start, feat_end, resist;
    if (dmg_type == damage_type_acid) {
        resist = feat_resist_energy_acid;
        feat_start = feat_epic_energy_resistance_acid_1;
        feat_end = feat_epic_energy_resistance_acid_10;
    } else if (dmg_type == damage_type_cold) {
        resist = feat_resist_energy_cold;
        feat_start = feat_epic_energy_resistance_cold_1;
        feat_end = feat_epic_energy_resistance_cold_10;
    } else if (dmg_type == damage_type_electrical) {
        resist = feat_resist_energy_electrical;
        feat_start = feat_epic_energy_resistance_electrical_1;
        feat_end = feat_epic_energy_resistance_electrical_10;
    } else if (dmg_type == damage_type_fire) {
        resist = feat_resist_energy_fire;
        feat_start = feat_epic_energy_resistance_fire_1;
        feat_end = feat_epic_energy_resistance_fire_10;
    } else if (dmg_type == damage_type_sonic) {
        resist = feat_resist_energy_sonic;
        feat_start = feat_epic_energy_resistance_sonic_1;
        feat_end = feat_epic_energy_resistance_sonic_10;
    } else {
        return {};
    }

    auto nth = highest_feat_in_range(cre, feat_start, feat_end);
    if (nth == nw::Feat::invalid()) {
        if (cre->stats.has_feat(resist)) {
            return 5;
        } else {
            return {};
        }
    }

    return (*nth - *feat_start + 1) * 10;
}

nw::ModifierResult toughness(const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    if (!cre) {
        return 0;
    }

    if (cre->stats.has_feat(feat_toughness)) {
        return cre->levels.level();
    }

    return 0;
}

nw::ModifierResult epic_toughness(const nw::ObjectBase* obj)
{
    auto nth = highest_feat_in_range(obj->as_creature(), feat_epic_toughness_1, feat_epic_toughness_10);
    if (nth == nw::Feat::invalid()) {
        return 0;
    }
    return (*nth - *feat_epic_toughness_1 + 1) * 20;
}

} // namespace nwn1
