#include "funcs_armor_class.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/Item.hpp>
#include <nw/kernel/TwoDACache.hpp>

namespace nwn1 {

int calculate_ac_versus(const nw::Creature* obj, nw::ObjectBase* versus, bool is_touch_attack)
{
    return 0;
}

int calculate_ac(const nw::Item* obj)
{
    if (!obj) { return 0; }

    if (obj->model_type == nw::ItemModelType::armor) {
        auto tda = nw::kernel::services().get_mut<nw::kernel::TwoDACache>();
        if (!tda) return 0;

        auto parts_chest = tda->get("parts_chest");
        if (!parts_chest) return 0;

        float temp = 0.0f;
        if (parts_chest->get_to(obj->model_parts[nw::ItemModelParts::armor_torso], "ACBonus", temp)) {
            return static_cast<int>(temp);
        } else {
            return 0;
        }
    }
    return 0;
}

} // namespace nwn1
