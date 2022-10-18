#include "funcs_combat.hpp"

#include "funcs_class.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/Item.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/Class.hpp>
#include <nw/util/macros.hpp>

namespace nwn1 {

int attack_bonus(const nw::Creature* obj, bool base)
{
    ROLLNW_UNUSED(base);
    int result = 0;
    if (!obj) {
        return result;
    }

    auto& classes = nw::kernel::rules().classes;

    // [TODO] This is obviously pretty wrong.
    for (const auto& cl : obj->levels.entries) {
        size_t cl_id = static_cast<size_t>(cl.id);
        if (!classes.entries[cl_id].attack_bonus_table) {
            continue;
        }
        result += (*classes.entries[cl_id].attack_bonus_table)[cl.level];
    }

    return result;
}

int number_of_attacks(const nw::Creature* obj, bool offhand)
{
    int ab = attack_bonus(obj, true);
    // int iter = weapon_iteration(ent, )
    return ab / 5;
}

int weapon_iteration(const nw::Creature* obj, nw::Item* weapon)
{
    if (!obj) {
        return 0;
    }

    auto& bia = nw::kernel::rules().baseitems;

    auto baseitem = bia.get(nw::BaseItem::make(weapon->baseitem));
    if (!baseitem) {
        return 0;
    }

    auto [yes, level] = can_use_monk_abilities(obj);
    if (baseitem->is_monk_weapon && yes) {
        return 3;
    }

    return 5;
}

} // namespace nwn1
