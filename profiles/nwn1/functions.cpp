#include "functions.hpp"
#include "functions/funcs_effects.hpp"

#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/util/templates.hpp>

namespace nwn1 {

// [TODO] Check polymorph, creature size for weapons
bool can_equip_item(const nw::Creature* obj, nw::Item* item, nw::EquipIndex slot)
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
    process_item_properties(obj, item, slot, false);
    return true;
}

nw::Item* get_equipped_item(const nw::Creature* obj, nw::EquipIndex slot)
{
    nw::Item* result = nullptr;
    if (!obj) { return result; }

    auto& it = obj->equipment.equips[size_t(slot)];
    if (alt<nw::Item*>(it)) {
        result = std::get<nw::Item*>(it);
    }
    return result;
}

std::string itemprop_to_string(const nw::ItemProperty& ip)
{
    std::string result;
    if (ip.type == std::numeric_limits<uint16_t>::max()) { return result; }
    auto type = nw::ItemPropertyType::make(ip.type);
    auto def = nw::kernel::rules().ip_definition(type);
    if (!def) { return nullptr; }

    result = nw::kernel::strings().get(def->game_string);

    if (ip.subtype != std::numeric_limits<uint16_t>::max() && def->subtype) {
        if (auto name = def->subtype->get<uint32_t>(ip.subtype, "Name")) {
            result += " " + nw::kernel::strings().get(*name);
        }
    }

    if (ip.cost_value != std::numeric_limits<uint16_t>::max() && def->cost_table) {
        if (auto name = def->cost_table->get<uint32_t>(ip.cost_value, "Name")) {
            result += " " + nw::kernel::strings().get(*name);
        }
    }

    if (ip.param_value != std::numeric_limits<uint8_t>::max() && def->param_table) {
        if (auto name = def->param_table->get<uint32_t>(ip.param_value, "Name")) {
            result += " " + nw::kernel::strings().get(*name);
        }
    }

    return result;
}

int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove)
{
    if (!obj || !item) { return 0; }

    int processed = 0;
    if (!remove) {
        for (const auto& ip : item->properties) {
            if (auto eff = nw::kernel::effects().generate(ip, index)) {
                eff->creator = item->handle();
                eff->category = nw::EffectCategory::item;
                if (!apply_effect(obj, eff)) {
                    nw::kernel::effects().destroy(eff);
                }
                ++processed;
            }
        }
        return processed;
    } else {
        return remove_effects_by(obj, item->handle());
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
        process_item_properties(obj, result, slot, true);
    }
    return result;
}

} // namespace nwn1
