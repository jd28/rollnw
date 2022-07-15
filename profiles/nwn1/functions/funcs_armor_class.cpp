#include "funcs_armor_class.hpp"

#include <nw/components/TwoDACache.hpp>
#include <nw/kernel/Kernel.hpp>

namespace nwn1 {

int calculate_ac_versus(flecs::entity ent, flecs::entity versus, bool is_touch_attack)
{
    return 0;
}

int calculate_ac(nw::EntityView<nw::Item> ent)
{
    if (!ent) {
        return 0;
    }
    auto it = ent.get<nw::Item>();
    if (it->model_type == nw::ItemModelType::armor) {
        auto tda = nw::kernel::world().get_mut<nw::TwoDACache>();
        if (!tda) {
            return 0;
        }
        auto parts_chest = tda->get("parts_chest");
        if (!parts_chest) {
            return 0;
        }
        float temp = 0.0f;
        if (parts_chest->get_to(it->model_parts[nw::ItemModelParts::armor_torso], "ACBonus", temp)) {
            return static_cast<int>(temp);
        } else {
            return 0;
        }
    }
    return 0;
}

} // namespace nwn1
