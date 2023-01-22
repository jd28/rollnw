#include "rules.hpp"

#include "combat.hpp"
#include "constants.hpp"
#include "functions.hpp"

#include <nw/functions.hpp>
#include <nw/objects/Creature.hpp>

namespace nwn1 {

// == Qualifiers ==============================================================
// ============================================================================

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
        case nw::SelectorType::bab: {
            auto val = value.as<int32_t>();
            auto min = qual.params[0].as<int32_t>();
            auto max = qual.params[1].as<int32_t>();
            if (val < min || (max != 0 && val > max)) {
                return false;
            }
            return true;
        }
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

// == Selectors ===============================================================
// ============================================================================

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
        if (!cre) { return {}; }

        if (selector.subtype.as<int32_t>() == 0x1) {
            return cre->lawful_chaotic;
        } else if (selector.subtype.as<int32_t>() == 0x2) {
            return cre->good_evil;
        } else {
            return -1;
        }
    }
    case nw::SelectorType::bab: {
        auto cre = obj->as_creature();
        if (!cre) { return {}; }
        return base_attack_bonus(cre);
    }
    case nw::SelectorType::class_level: {
        if (!selector.subtype.is<int32_t>()) {
            LOG_F(ERROR, "selector - class_level: invalid subtype");
            return {};
        }

        auto cre = obj->as_creature();
        if (!cre) { return {}; }

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
        if (!cre) { return {}; }

        return cre->stats.has_feat(nw::Feat::make(selector.subtype.as<int32_t>()));
    }
    case nw::SelectorType::hitpoints_max:
        return get_max_hitpoints(obj);
    case nw::SelectorType::level: {
        auto cre = obj->as_creature();
        if (!cre) { return {}; }
        return cre->levels.level();
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

} // namespace nwn1
