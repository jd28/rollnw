#include "functions.hpp"

#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/util/templates.hpp>

namespace nwn1 {

// [TODO] Check polymorph, creature size for weapons
bool can_equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot)
{
    if (!obj || !item) { return false; }
    auto& bia = nw::kernel::rules().baseitems;

    auto baseitem = bia.get(nw::BaseItem::make(item->baseitem));
    if (!baseitem) { return false; }

    auto flag = 1 << size_t(slot);
    return baseitem->equipable_slots & flag;
}

bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot)
{
    if (!obj || !item) { return false; }
    if (!can_equip_item(obj, item, slot)) { return false; }
    unequip_item(obj, slot);
    obj->equipment.equips[size_t(slot)] = item;
    process_item_properties(obj, item, false);
    return true;
}

nw::Item* get_equipped_item(nw::Creature* obj, nw::EquipIndex slot)
{
    nw::Item* result = nullptr;
    if (!obj) { return result; }

    auto& it = obj->equipment.equips[size_t(slot)];
    if (alt<nw::Item*>(it)) {
        result = std::get<nw::Item*>(it);
    }
    return result;
}

int process_item_properties(nw::Creature* obj, const nw::Item* item, bool remove)
{
    if (!obj || !item) { return 0; }

    int processed = 0;
    if (!remove) {
        for (const auto& ip : item->properties) {
            if (auto eff = nw::kernel::effects().generate(ip)) {
                eff->creator = item->handle();
                nw::kernel::events().add(nw::kernel::EventType::apply_effect, obj, eff);
                ++processed;
            }
        }
        return processed;
    } else {
        return queue_remove_effect_by(obj, item->handle());
    }
}

int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    int processed = 0;
    for (const auto& handle : obj->effects()) {
        if (handle.effect->creator == creator) {
            nw::kernel::events().add(nw::kernel::EventType::remove_effect, obj, handle.effect);
            ++processed;
        }
    }
    return processed;
}

nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot)
{
    nw::Item* result = nullptr;
    if (!obj) { return result; }

    auto& it = obj->equipment.equips[size_t(slot)];
    if (alt<nw::Item*>(it)) {
        result = std::get<nw::Item*>(it);
        it = nullptr;
        process_item_properties(obj, result, true);
    }
    return result;
}

} // namespace nwn1
