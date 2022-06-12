#include "rules.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../log.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/components/Common.hpp"
#include "../../objects/components/CreatureStats.hpp"
#include "../../objects/components/LevelStats.hpp"
#include "../../rules/Feat.hpp"
#include "constants.hpp"

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
            if (ge_sel.is<int32_t>()) { ge = ge_sel.as<int32_t>(); }

            auto lc_sel = selector(sel::alignment(nw::AlignmentAxis::law_chaos), cre);
            if (lc_sel.is<int32_t>()) { lc = lc_sel.as<int32_t>(); }

            if (!!(flags & nw::AlignmentFlags::good) && !!(target_axis | nw::AlignmentAxis::good_evil)) {
                if (ge > 50) { return true; }
            }

            if (!!(flags & nw::AlignmentFlags::evil) && !!(target_axis | nw::AlignmentAxis::good_evil)) {
                if (ge < 50) { return true; }
            }

            if (!!(flags & nw::AlignmentFlags::lawful) && !!(target_axis | nw::AlignmentAxis::law_chaos)) {
                if (lc > 50) { return true; }
            }

            if (!!(flags & nw::AlignmentFlags::chaotic) && !!(target_axis | nw::AlignmentAxis::law_chaos)) {
                if (lc < 50) { return true; }
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
            if (val < min || (max != 0 && val > max)) { return false; }
            return true;
        }
        case nw::SelectorType::feat: {
            return value.is<int32_t>() && value.as<int32_t>();
        }
        case nw::SelectorType::race: {
            auto val = size_t(value.as<int32_t>());
            return val == qual.params[0].as<nw::Index>();
        }
        case nw::SelectorType::skill:
        case nw::SelectorType::ability: {
            auto val = value.as<int32_t>();
            auto min = qual.params[0].as<int32_t>();
            auto max = qual.params[1].as<int32_t>();
            if (val < min) { return false; }
            if (max != 0 && val > max) { return false; }
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
        if (!selector.subtype.is<nw::Index>()) {
            LOG_F(ERROR, "selector - ability: invalid subtype");
            return {};
        }
        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) { return {}; }
        return static_cast<int>(stats->abilities[selector.subtype.as<nw::Index>()]);
    }
    case nw::SelectorType::alignment: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - alignment: invalid subtype");
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
        if (!selector.subtype.is<nw::Index>()) {
            LOG_F(ERROR, "selector - class_level: invalid subtype");
            return {};
        }

        auto stats = ent.get<nw::LevelStats>();
        if (!stats) { return 0; }
        for (const auto& ce : stats->entries) {
            if (ce.id == selector.subtype.as<nw::Index>()) {
                return ce.level;
            }
        }
        return 0;
    }
    case nw::SelectorType::feat: {
        if (!selector.subtype.is<nw::Index>()) {
            LOG_F(ERROR, "selector - feat: invalid subtype");
            return {};
        }

        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) { return 0; }
        return stats->has_feat(selector.subtype.as<nw::Index>());
    }
    case nw::SelectorType::level: {
        auto levels = ent.get<nw::LevelStats>();
        if (!levels) { return 0; }

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
        if (!common) { return {}; }
        return common->locals.get_int(selector.subtype.as<std::string>());
    }
    case nw::SelectorType::local_var_str: {
        auto common = ent.get<nw::Common>();
        if (!selector.subtype.is<std::string>()) {
            LOG_F(ERROR, "selector - local_var_str: invalid subtype");
            return {};
        }
        if (!common) { return {}; }
        return common->locals.get_string(selector.subtype.as<std::string>());
    }
    case nw::SelectorType::race: {
        auto c = ent.get<nw::Creature>();
        if (!c) { return {}; }
        return static_cast<int>(c->race);
    }
    case nw::SelectorType::skill: {
        if (!selector.subtype.is<nw::Index>()) {
            LOG_F(ERROR, "selector - skill: invalid subtype");
            return {};
        }

        auto stats = ent.get<nw::CreatureStats>();
        if (!stats) { return {}; }
        return static_cast<int>(stats->skills[selector.subtype.as<nw::Index>()]);
    }
    }

    return {};
}

std::pair<nw::Index, int> has_feat_successor(flecs::entity ent, nw::Index feat)
{
    nw::Index highest;
    int count = 0;

    auto featarray = nw::kernel::world().get<nw::FeatArray>();
    if (!featarray) { return {highest, count}; }

    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) { return {highest, count}; }

    while (stats->has_feat(feat)) { // [TODO] Make this more efficient
        highest = feat;
        ++count;

        const auto& next_entry = featarray->entries[feat];
        if (next_entry.successor == std::numeric_limits<size_t>::max()) {
            break;
        }
        feat = featarray->entries[next_entry.successor].index;
    }

    return {highest, count};
}

size_t highest_feat_in_range(flecs::entity ent, size_t start, size_t end)
{
    auto stats = ent.get<nw::CreatureStats>();
    if (!stats) { return ~0u; }
    while (end >= start) {
        if (stats->has_feat(end)) { return end; }
        --end;
    }
    return ~0u;
}

nw::ModifierResult dragon_disciple_ac(flecs::entity ent)
{
    auto stat = ent.get<nw::LevelStats>();
    if (!stat) { return 0; }
    auto level = stat->level_by_class(nwn1::class_type_dragon_disciple);
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
    if (!stat) { return 0; }
    auto pm_level = stat->level_by_class(nwn1::class_type_pale_master);
    return ((pm_level / 4) + 1) * 2;
}

nw::ModifierResult epic_toughness(flecs::entity ent)
{
    auto nth = highest_feat_in_range(ent, feat_epic_toughness_1, feat_epic_toughness_10);
    if (nth == ~0u) { return 0; }
    return int(nth - feat_epic_toughness_1 + 1) * 20;
}

} // namespace nwn1
