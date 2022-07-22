#include "rules.hpp"

#include "constants.hpp"
#include "functions.hpp"

#include <nw/components/Common.hpp>
#include <nw/components/Creature.hpp>
#include <nw/components/CreatureStats.hpp>
#include <nw/components/LevelStats.hpp>
#include <nw/components/TwoDACache.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Feat.hpp>

namespace nwn1 {

bool match(const nw::Qualifier& qual, const flecs::entity cre)
{
    auto value = selector(qual.selector, cre);
    if (!value.empty()) {
        switch (qual.selector.type) {
        default:
            return false;
        case nw::SelectorType::alignment: {
            auto target_axis = static_cast<nw::AlignmentAxis>(qual.selector.subtype.as<int32_t>());
            auto flags = static_cast<nw::AlignmentFlags>(qual.params[0].as<int32_t>());
            auto ge = 50;
            auto lc = 50;

            auto ge_sel = selector(sel::alignment(nw::AlignmentAxis::good_evil), cre);
            if (ge_sel.is<int32_t>()) {
                ge = ge_sel.as<int32_t>();
            }

            auto lc_sel = selector(sel::alignment(nw::AlignmentAxis::law_chaos), cre);
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

nw::RuleValue selector(const nw::Selector& selector, const flecs::entity ent)
{
    switch (selector.type) {
    default:
        return {};
    case nw::SelectorType::ability: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - ability: invalid subtype");
            return {};
        }
        return get_ability_score(ent, nw::make_ability(selector.subtype.as<int32_t>()));
    }
    case nw::SelectorType::alignment: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - alignment: invalid subtype");
            return {};
        }
        auto cre = ent.get<nw::Creature>();
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

        auto stats = ent.get<nw::LevelStats>();
        if (!stats) {
            return 0;
        }
        for (const auto& ce : stats->entries) {
            if (ce.id == nw::make_class(selector.subtype.as<int32_t>())) {
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

        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) {
            return 0;
        }
        return stats->has_feat(nw::make_feat(selector.subtype.as<int32_t>()));
    }
    case nw::SelectorType::level: {
        auto levels = ent.get<nw::LevelStats>();
        if (!levels) {
            return 0;
        }

        int level = 0;
        for (const auto& ce : levels->entries) {
            level += ce.level;
        }
        return level;
    }
    case nw::SelectorType::local_var_int: {
        auto common = ent.get<nw::Common>();
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
        auto common = ent.get<nw::Common>();
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
        auto c = ent.get<nw::Creature>();
        if (!c) {
            return {};
        }
        return static_cast<int>(c->race);
    }
    case nw::SelectorType::skill: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - skill: invalid subtype");
            return {};
        }

        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) {
            return {};
        }
        return static_cast<int>(stats->skills[selector.subtype.as<int32_t>()]);
    }
    }

    return {};
}

nw::ModifierResult epic_great_strength(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_great_strength_1, feat_epic_great_strength_10);
    if (nth == nw::Feat::invalid) {
        return 0;
    }
    return int(nth) - int(feat_epic_great_strength_1) + 1;
}

nw::ModifierResult epic_great_dexterity(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_great_dexterity_1, feat_epic_great_dexterity_10);
    if (nth == nw::Feat::invalid) {
        return 0;
    }
    return int(nth) - int(feat_epic_great_dexterity_1) + 1;
}

nw::ModifierResult epic_great_constitution(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_great_constitution_1, feat_epic_great_constitution_10);
    if (nth == nw::Feat::invalid) {
        return 0;
    }
    return int(nth) - int(feat_epic_great_constitution_1) + 1;
}

nw::ModifierResult epic_great_intelligence(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_great_intelligence_1, feat_epic_great_intelligence_10);
    if (nth == nw::Feat::invalid) {
        return 0;
    }
    return int(nth) - int(feat_epic_great_intelligence_1) + 1;
}

nw::ModifierResult epic_great_wisdom(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_great_wisdom_1, feat_epic_great_wisdom_10);
    if (nth == nw::Feat::invalid) {
        return 0;
    }
    return int(nth) - int(feat_epic_great_wisdom_1) + 1;
}

nw::ModifierResult epic_great_charisma(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_great_charisma_1, feat_epic_great_charisma_10);
    if (nth == nw::Feat::invalid) {
        return 0;
    }
    return int(nth) - int(feat_epic_great_charisma_1) + 1;
}

nw::ModifierResult dragon_disciple_ac(flecs::entity ent)
{
    // auto cls = nw::kernel::world().get<nw::ClassArray>();
    //  if (!cls) { return 0; }
    //  if (cls->entries.size() < nwn1::class_type_dragon_disciple) {
    //      return 0;
    //  }
    //  if (cls->entries[nwn1::class_type_dragon_disciple].index != nwn1::class_type_dragon_disciple) {
    //      return 0;
    //  }

    auto stat = ent.get<nw::LevelStats>();
    if (!stat) {
        return 0;
    }
    auto level = stat->level_by_class(nwn1::class_type_dragon_disciple);

    // auto tdas = nw::kernel::world().get_mut<nw::TwoDACache>();
    // if (tdas) {
    //     auto cls_stat_2da = tdas->get(cls->entries[nwn1::class_type_dragon_disciple].stat_gain_table);
    //     if (cls_stat_2da) {
    //         int res = 0, tmp;
    //         for (int i = 0; i < level; ++i) {
    //             if (cls_stat_2da->get_to(i, "NaturalAC", tmp)) {
    //                 res += tmp;
    //             }
    //         }
    //         return res;
    //     }
    // }

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

nw::ModifierResult pale_master_ac(flecs::entity ent)
{
    auto stat = ent.get<nw::LevelStats>();
    if (!stat) {
        return 0;
    }
    auto pm_level = stat->level_by_class(nwn1::class_type_pale_master);

    // auto cls = nw::kernel::world().get<nw::ClassArray>();
    // if (!cls) { return 0; }
    // if (cls->entries.size() < nwn1::class_type_pale_master) {
    //     return 0;
    // }
    // if (cls->entries[nwn1::class_type_pale_master].index != nwn1::class_type_pale_master) {
    //     return 0;
    // }

    // auto tdas = nw::kernel::world().get_mut<nw::TwoDACache>();
    // if (tdas) {
    //     auto cls_stat_2da = tdas->get(cls->entries[nwn1::class_type_pale_master].stat_gain_table);
    //     if (cls_stat_2da) {
    //         int res = 0, tmp;
    //         for (int i = 0; i < pm_level; ++i) {
    //             if (cls_stat_2da->get_to(i, "NaturalAC", tmp)) {
    //                 res += tmp;
    //             }
    //         }
    //         return res;
    //     }
    // }

    return pm_level > 0 ? ((pm_level / 4) + 1) * 2 : 0;
}

nw::ModifierResult toughness(flecs::entity ent)
{
    nw::EntityView<nw::CreatureStats, nw::LevelStats> cre(ent);
    if (!cre) {
        return 0;
    }

    if (cre.get<nw::CreatureStats>()->has_feat(feat_toughness)) {
        return cre.get<nw::LevelStats>()->level();
    }

    return 0;
}

nw::ModifierResult epic_toughness(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_toughness_1, feat_epic_toughness_10);
    if (nth == nw::Feat::invalid) {
        return 0;
    }
    return (int(nth) - int(feat_epic_toughness_1) + 1) * 20;
}

} // namespace nwn1
