#include "funcs_armor_class.hpp"

#include "../constants.hpp"
#include "../functions.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/Item.hpp>
#include <nw/functions.hpp>
#include <nw/kernel/TwoDACache.hpp>

namespace nwn1 {

int calculate_ac_versus(const nw::Creature* obj, nw::ObjectBase* versus, bool is_touch_attack)
{
    return 0;
}

int calculate_item_ac(const nw::Item* obj)
{
    if (!obj) { return 0; }
    if (obj->baseitem == base_item_armor && obj->armor_id != -1) {
        // [TODO] Optimize
        auto& tda = nw::kernel::twodas();
        if (auto armor = tda.get("armor")) {
            if (auto result = armor->get<int32_t>(obj->armor_id, "ACBONUS")) {
                return *result;
            }
        }
    } else if (is_shield(obj->baseitem)) {
        // [TODO] Figure this out
        return 0;
    }

    return 0;
}

} // namespace nwn1
