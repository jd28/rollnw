#include "rules.hpp"

#include "combat.hpp"
#include "constants.hpp"
#include "functions.hpp"

#include "../../functions.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"

namespace nwn1 {

bool qualify_ability(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - ability: invalid subtype");
        return {};
    }
    auto val = get_ability_score(obj->as_creature(), nw::Ability::make(qual.subtype.as<int32_t>()));
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_alignment(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - alignment: invalid subtype");
        return false;
    }

    auto cre = obj->as_creature();
    if (!cre) { return false; }

    auto target_axis = static_cast<nw::AlignmentAxis>(qual.subtype.as<int32_t>());
    auto flags = static_cast<nw::AlignmentFlags>(qual.params[0].as<int32_t>());
    auto ge = cre->good_evil;
    auto lc = cre->lawful_chaotic;

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

    return false;
}

bool qualify_bab(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto val = base_attack_bonus(obj->as_creature());
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_feat(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    return cre && cre->stats.has_feat(nw::Feat::make(qual.subtype.as<int32_t>()));
}

bool qualify_class_level(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - ability: invalid subtype");
        return false;
    }

    auto cre = obj->as_creature();
    if (!cre) { return false; }
    auto val = cre->levels.level_by_class(nw::Class::make(qual.subtype.as<int32_t>()));
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_level(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    if (!cre) { return false; }
    auto val = cre->levels.level();
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_race(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    return cre && *cre->race == qual.params[0].as<int32_t>();
}

bool qualify_skill(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - skill: invalid subtype");
        return {};
    }
    auto val = get_skill_rank(obj->as_creature(), nw::Skill::make(qual.subtype.as<int32_t>()));
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();
    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

void load_qualifiers()
{
    nw::kernel::rules().set_qualifier(nw::req_type_ability, qualify_ability);
    nw::kernel::rules().set_qualifier(nw::req_type_alignment, qualify_alignment);
    nw::kernel::rules().set_qualifier(nw::req_type_bab, qualify_bab);
    nw::kernel::rules().set_qualifier(nw::req_type_class_level, qualify_class_level);
    nw::kernel::rules().set_qualifier(nw::req_type_feat, qualify_feat);
    nw::kernel::rules().set_qualifier(nw::req_type_level, qualify_level);
    nw::kernel::rules().set_qualifier(nw::req_type_race, qualify_race);
    nw::kernel::rules().set_qualifier(nw::req_type_skill, qualify_skill);
}

} // namespace nwn1
