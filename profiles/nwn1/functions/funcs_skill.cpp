#include "funcs_skill.hpp"

#include "../constants.hpp"
#include "funcs_ability.hpp"
#include "funcs_feat.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/rules/MasterFeat.hpp>
#include <nw/util/EntityView.hpp>

namespace nwn1 {

int get_skill_rank(flecs::entity ent, nw::Skill skill, bool base)
{
    int result = 0;
    bool untrained = true;

    auto skill_array = nw::kernel::world().get<nw::SkillArray>();
    auto ski = skill_array->get(skill);
    if (!ski) {
        LOG_F(WARNING, "attempting to get skill rank of invalid skill: {}", int(skill));
        return 0;
    }

    nw::EntityView<nw::CreatureStats> cre{ent};
    if (!cre) return 0;
    auto stats = cre.get<nw::CreatureStats>();

    // Base
    result = stats->skills[static_cast<size_t>(skill)];
    if (base) return result;

    if (result == 0) {
        untrained = ski->untrained;
    }

    if (untrained) {
        auto ability = get_ability_score(cre, ski->ability) - 10;
        if (ability >= 2) {
            result += ability / 2;
        }
    }

    // Feats
    auto mf_bonus = resolve_master_feats<int>(cre, skill,
        mfeat_skill_focus, mfeat_skill_focus_epic);

    for (auto b : mf_bonus) {
        result += b;
    }

    // Effects

    return result;
}

} // namespace nwn1
