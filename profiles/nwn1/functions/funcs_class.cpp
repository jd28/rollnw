#include "funcs_class.hpp"

#include "../constants.hpp"

#include <nw/rules/Class.hpp>

namespace nwn1 {

std::pair<bool, int> can_use_monk_abilities(flecs::entity ent)
{
    if (class_type_monk == nw::Class::invalid) {
        return {false, 0};
    }
    nw::EntityView<nw::Creature, nw::LevelStats, nw::Equips> cre{ent};
    if (!cre) {
        return {false, 0};
    }
    auto level = cre.get<nw::LevelStats>()->level_by_class(class_type_monk);
    if (level == 0) {
        return {false, 0};
    }

    //        if not cre:GetIsPolymorphed() then
    //       local chest = cre:GetItemInSlot(INVENTORY_SLOT_CHEST)
    //       if chest:GetIsValid() and chest:ComputeArmorClass() > 0 then
    //          return false, level
    //       end

    //       local shield = cre:GetItemInSlot(INVENTORY_SLOT_LEFTHAND)
    //       if shield:GetIsValid() and
    //          (shield:GetBaseType() == BASE_ITEM_SMALLSHIELD
    //           or shield:GetBaseType() == BASE_ITEM_LARGESHIELD
    //           or shield:GetBaseType() == BASE_ITEM_TOWERSHIELD)
    //       then
    //          return false, level
    //       end
    //    end

    return {true, level};
}

} // namespace nwn1
