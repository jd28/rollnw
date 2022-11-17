#include "funcs_skill.hpp"

#include "../constants.hpp"
#include "funcs_ability.hpp"
#include "funcs_effects.hpp"
#include "funcs_feat.hpp"

#include <nw/components/Creature.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/feats.hpp>

namespace nwn1 {

int get_skill_rank(const nw::Creature* obj, nw::Skill skill, bool base)
{
    if (!obj) { return 0; }

    int result = 0;
    auto adder = [&result](int value) { result += value; };

    auto& skill_array = nw::kernel::rules().skills;
    auto ski = skill_array.get(skill);
    if (!ski) {
        LOG_F(WARNING, "attempting to get skill rank of invalid skill: {}", *skill);
        return 0;
    }

    // Base
    result = obj->stats.get_skill_rank(skill);
    if (base) return result;

    bool untrained = true;
    if (result == 0) { untrained = ski->untrained; }
    if (untrained) {
        result += get_ability_modifier(obj, ski->ability);
    }

    // Master Feats
    nw::kernel::resolve_master_feats<int>(obj, skill, adder,
        mfeat_skill_focus, mfeat_skill_focus_epic);

    // Modifiers
    nw::kernel::resolve_modifier(obj, mod_type_skill, skill, adder);

    // Effects
    auto end = std::end(obj->effects());
    int value = 0;

    auto callback = [&value](int mod) { value += mod; };

    auto it = resolve_effects_of<int>(std::begin(obj->effects()), end,
        effect_type_skill_increase, *skill, callback, &effect_extract_int0);

    int bonus = value;
    value = 0; // Reset value for penalties

    resolve_effects_of<int>(it, end, effect_type_skill_decrease, *skill,
        callback, &effect_extract_int0);
    int decrease = value;

    return result + std::clamp(bonus - decrease, -50, 50);
}

} // namespace nwn1
