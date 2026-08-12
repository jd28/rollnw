#include "../runtime.hpp"

#include "../../kernel/Rules.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"

#include <algorithm>
#include <limits>

namespace nw::smalls {

namespace {

struct ScriptAppearanceInfo {
    int32_t id = -1;
    nw::Resref model;
    int32_t model_type = -1;
};

ScriptAppearanceInfo appearance_info(int32_t id)
{
    const auto* info = nw::kernel::rules().appearances.get(nw::Appearance::make(id));
    if (!info) { return {}; }
    return {
        .id = id,
        .model = info->model,
        .model_type = static_cast<int32_t>(info->model_type),
    };
}

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_creature();
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
        .native_struct<ScriptAppearanceInfo>("AppearanceInfo")
        .field("id", &ScriptAppearanceInfo::id)
        .field("model", &ScriptAppearanceInfo::model)
        .field("model_type", &ScriptAppearanceInfo::model_type)
        .end_struct()
        .function("appearance_info", &appearance_info)
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
        .function("set_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t slot, int32_t ability, int32_t modifier, int32_t flags) -> bool {
            if (!as_creature(obj) || flags < 0) { return false; }
            return nw::kernel::objects().components().set_slotted_ability(
                obj, source, tier, slot, ability, modifier, static_cast<uint32_t>(flags)); })
        .function("clear_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t slot, int32_t ability, int32_t modifier, int32_t flags) -> bool {
            if (!as_creature(obj) || flags < 0) { return false; }
            auto& components = nw::kernel::objects().components();
            const auto* loadout = components.find_ability_loadout(obj);
            if (!loadout) { return false; }
            const auto entry = std::find_if(loadout->entries.begin(), loadout->entries.end(),
                [=](const auto& candidate) {
                    return candidate.source == source && candidate.tier == tier
                        && candidate.slot == slot && candidate.ability == ability
                        && candidate.modifier == modifier
                        && candidate.flags == static_cast<uint32_t>(flags);
                });
            if (entry == loadout->entries.end()) { return false; }
            components.clear_slotted_ability(obj, source, tier, slot);
            return true; })
        .function("remove_slotted_ability", +[](nw::ObjectHandle obj, int32_t source, int32_t tier, int32_t ability, int32_t modifier) -> bool {
            if (!as_creature(obj)) { return false; }
            auto& components = nw::kernel::objects().components();
            const int32_t slot = components.find_slotted_ability_slot(
                obj, source, tier, ability, modifier);
            if (slot < 0) { return false; }
            components.clear_slotted_ability(obj, source, tier, slot);
            return true; })
        .function("clear_slotted_ability_from_tier", +[](nw::ObjectHandle obj, int32_t source, int32_t min_tier, int32_t ability) -> bool {
            if (!as_creature(obj)) { return false; }
            nw::kernel::objects().components().clear_slotted_ability_from_tier(obj, source, min_tier, ability);
            return true; })
        // The end.
        .finalize();
}

} // namespace nw::smalls
