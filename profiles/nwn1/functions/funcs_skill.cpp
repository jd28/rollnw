#include "funcs_skill.hpp"

#include "../constants.hpp"
#include "funcs_ability.hpp"
#include "funcs_feat.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/MasterFeat.hpp>

namespace nwn1 {

int get_skill_rank(const nw::Creature* obj, nw::Skill skill, bool base)
{
    if (!obj) return 0;

    int result = 0;
    bool untrained = true;

    auto& skill_array = nw::kernel::rules().skills;
    auto ski = skill_array.get(skill);
    if (!ski) {
        LOG_F(WARNING, "attempting to get skill rank of invalid skill: {}", int(skill));
        return 0;
    }

    // Base
    result = obj->stats.skills[static_cast<size_t>(skill)];
    if (base) return result;

    if (result == 0) {
        untrained = ski->untrained;
    }

    if (untrained) {
        result += get_ability_modifier(obj, ski->ability);
    }

    // Feats
    auto& mfr = nw::kernel::rules().master_feats;
    auto mf_bonus = mfr.resolve<int>(obj, skill, mfeat_skill_focus, mfeat_skill_focus_epic);

    for (auto b : mf_bonus) {
        result += b;
    }

    // Effects

    return result;
}

} // namespace nwn1
