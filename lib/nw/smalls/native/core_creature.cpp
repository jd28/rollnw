#include "../runtime.hpp"

#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/Equips.hpp"
#include "../../objects/ObjectManager.hpp"

#include <limits>

namespace nw::smalls {

namespace {

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_creature();
}

nw::Item* as_item(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_item();
}

const nw::ObjectAbilityLoadoutEntry* ability_loadout_entry(nw::ObjectHandle obj, int32_t index)
{
    if (index < 0) { return nullptr; }
    auto* row = nw::kernel::objects().components().find_ability_loadout(obj);
    if (!row || static_cast<size_t>(index) >= row->entries.size()) { return nullptr; }
    return &row->entries[static_cast<size_t>(index)];
}

} // namespace

void register_core_creature(Runtime& rt)
{
    if (rt.get_native_module("core.creature")) {
        return;
    }

    rt.module("core.creature")
        .function("set_vitals", +[](nw::ObjectHandle obj, int32_t hp_current, int32_t hp_max) -> bool {
            if (!as_creature(obj)) { return false; }
            return nw::kernel::objects().components().set_vitals(obj, hp_current, hp_max); })
        .function("get_vitals_hp_current", +[](nw::ObjectHandle obj) -> int32_t {
            auto* row = nw::kernel::objects().components().find_vitals(obj);
            return row ? row->hp_current : 0; })
        .function("get_vitals_hp_max", +[](nw::ObjectHandle obj) -> int32_t {
            auto* row = nw::kernel::objects().components().find_vitals(obj);
            return row ? row->hp_max : 0; })
        .function("get_ability_loadout_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* row = nw::kernel::objects().components().find_ability_loadout(obj);
            if (!row || row->entries.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
                return 0;
            }
            return static_cast<int32_t>(row->entries.size()); })
        .function("get_ability_loadout_ability", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->ability : -1; })
        .function("get_ability_loadout_source", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->source : -1; })
        .function("get_ability_loadout_tier", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->tier : -1; })
        .function("get_ability_loadout_slot", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->slot : -1; })
        .function("get_ability_loadout_modifier", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* entry = ability_loadout_entry(obj, index);
            return entry ? entry->modifier : -1; })
        .function("add_unslotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability) -> bool {
            if (!as_creature(obj)) { return false; }
            return nw::kernel::objects().components().add_unslotted_ability(obj, source, tier, ability); })
        .function("remove_unslotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability) -> bool {
            if (!as_creature(obj)) { return false; }
            nw::kernel::objects().components().remove_unslotted_ability(obj, source, tier, ability);
            return true; })
        .function("set_slotted_ability_count", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t count) -> bool {
            if (!as_creature(obj) || count < 0) { return false; }
            return nw::kernel::objects().components().set_slotted_ability_count(
                obj, source, tier, static_cast<size_t>(count)); })
        .function("available_ability_slots", +[](nw::ObjectHandle obj, int32_t source, int32_t tier) -> int32_t {
            if (!as_creature(obj)) { return 0; }
            return nw::kernel::objects().components().available_ability_slots(obj, source, tier); })
        .function("add_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability, int32_t modifier, int32_t flags) -> bool {
            if (!as_creature(obj) || flags < 0) { return false; }
            return nw::kernel::objects().components().add_slotted_ability(
                obj, source, tier, ability, modifier, static_cast<uint32_t>(flags)); })
        .function("clear_slotted_ability_from_tier", +[](nw::ObjectHandle obj, int32_t source, int32_t min_tier, int32_t ability) -> bool {
            if (!as_creature(obj)) { return false; }
            nw::kernel::objects().components().clear_slotted_ability_from_tier(obj, source, min_tier, ability);
            return true; })
        .function("equip_item_in_slot", +[](nw::ObjectHandle creature, nw::ObjectHandle item, int32_t slot) -> bool { return nw::equip_item_in_slot(as_creature(creature), as_item(item), static_cast<nw::EquipIndex>(slot)); })
        .function("get_equipped_item", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nw::get_equipped_item(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("unequip_item_in_slot", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nw::unequip_item_in_slot(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("inventory_add_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item) -> bool {
            auto* cre = as_creature(creature);
            auto* it = as_item(item);
            if (!cre || !it) { return false; }
            return cre->inventory().add_item(it); })
        .function("inventory_remove_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item) -> bool {
            auto* cre = as_creature(creature);
            auto* it = as_item(item);
            if (!cre || !it) { return false; }
            return cre->inventory().remove_item(it); })

        // The end.
        .finalize();
}

} // namespace nw::smalls
