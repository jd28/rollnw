#include "funcs_combat.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/Item.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/rules/Class.hpp>
#include <nw/util/macros.hpp>

namespace nwn1 {

int attack_bonus(flecs::entity ent, bool base)
{
    ROLLNW_UNUSED(base);
    int result = 0;
    auto classes = nw::kernel::world().get<nw::ClassArray>();
    auto stats = ent.get<nw::LevelStats>();
    if (!classes || !stats) {
        return result;
    }
    // [TODO] This is obviously pretty wrong.
    for (const auto& cl : stats->entries) {
        size_t cl_id = static_cast<size_t>(cl.id);
        if (!classes->entries[cl_id].attack_bonus_table) {
            continue;
        }
        result += (*classes->entries[cl_id].attack_bonus_table)[cl.level];
    }

    return result;
}

int number_of_attacks(flecs::entity ent, bool offhand)
{
    int ab = attack_bonus(ent, true);
    // int iter = weapon_iteration(ent, )
    return ab / 5;
}

int weapon_iteration(flecs::entity ent, nw::EntityView<nw::Item> weapon)
{
    if (!weapon) {
        return 0;
    }
    auto bia = nw::kernel::world().get<nw::BaseItemArray>();
    if (!bia) {
        return 0;
    }
    auto baseitem = bia->get(nw::make_baseitem(weapon.get<nw::Item>()->baseitem));
    if (!baseitem) {
        return 0;
    }
    // if (baseitem->is_monk_weapon && can_use_monk_abilities(ent)) {
    //     return 3;
    // }
    return 5;
}

} // namespace nwn1
