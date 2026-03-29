#include "../stdlib.hpp"

#include "../../formats/StaticTwoDA.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../profiles/nwn1/propset_populate.hpp"
#include "../../profiles/nwn1/scriptapi.hpp"
#include "../../rules/effects.hpp"
#include "../../rules/items.hpp"
#include "../../scriptapi.hpp"

namespace nw::smalls {

namespace {

constexpr uint32_t item_feature_on_hit_properties = 1u << 0;

struct ScriptItemProperty {
    int32_t prop_type;
    int32_t subtype;
    int32_t cost_table;
    int32_t cost_value;
    int32_t param_table;
    int32_t param_value;
};

nw::Item* as_item(nw::ObjectHandle obj)
{
    return nw::kernel::objects().get<nw::Item>(obj);
}

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    return nw::kernel::objects().get<nw::Creature>(obj);
}

} // namespace

void register_core_item(Runtime& rt)
{
    if (rt.get_native_module("core.item")) {
        return;
    }

    rt.module("core.item")
        .native_struct<ScriptItemProperty>("ItemProperty")
        .field("prop_type", &ScriptItemProperty::prop_type)
        .field("subtype", &ScriptItemProperty::subtype)
        .field("cost_table", &ScriptItemProperty::cost_table)
        .field("cost_value", &ScriptItemProperty::cost_value)
        .field("param_table", &ScriptItemProperty::param_table)
        .field("param_value", &ScriptItemProperty::param_value)
        .end_struct()
        .function("property_count", +[](nw::ObjectHandle item) -> int32_t {
            auto* it = as_item(item);
            if (!it) { return 0; }
            return static_cast<int32_t>(it->properties.size()); })
        .function("clear_properties", +[](nw::ObjectHandle item) {
            auto* it = as_item(item);
            if (!it) {  return; }
            it->properties.clear();
            it->derived_flags = 0;
            it->derived_weapon_power_bonus = 0;
            it->derived_has_keen = false; })
        .function("has_on_hit_properties", +[](nw::ObjectHandle item) -> bool {
            auto* it = as_item(item);
            if (!it) { return false; }
            return (it->derived_flags & item_feature_on_hit_properties) != 0; })
        .function("has_property", +[](nw::ObjectHandle item, int32_t prop_type) -> bool {
            auto* it = as_item(item);
            if (!it) { return false; }
            return nw::item_has_property(it, nw::ItemPropertyType::make(prop_type)); })
        .function("__get_property", +[](nw::ObjectHandle item, int32_t index) -> ScriptItemProperty {
            auto* it = as_item(item);
            if (!it || index < 0 || static_cast<size_t>(index) >= it->properties.size()) {
                return {};
            }
            const auto& ip = it->properties[static_cast<size_t>(index)];
            return {static_cast<int32_t>(ip.type), static_cast<int32_t>(ip.subtype),
                    static_cast<int32_t>(ip.cost_table), static_cast<int32_t>(ip.cost_value),
                    static_cast<int32_t>(ip.param_table), static_cast<int32_t>(ip.param_value)}; })
        .function("__apply_item_effect", +[](nw::ObjectHandle creature_h, nw::ObjectHandle item_h, nw::TypedHandle effect_h) -> bool {
            auto* creature = as_creature(creature_h);
            auto* item = as_item(item_h);
            auto* eff = nw::kernel::effects().get(effect_h);
            if (!creature || !item || !eff) {
                return false;
            }
            eff->handle().creator = item->handle();
            eff->handle().category = nw::EffectCategory::item;
            nw::kernel::runtime().set_handle_ownership(effect_h, OwnershipMode::ENGINE_OWNED);
            if (!apply_effect(creature, eff)) {
                nw::kernel::effects().destroy(eff);
                return false;
            }
            return true; })
        .function("__remove_item_effects", +[](nw::ObjectHandle creature_h, nw::ObjectHandle item_h) -> int32_t {
            auto* creature = as_creature(creature_h);
            auto* item = as_item(item_h);
            if (!creature || !item) { return 0; }
            return remove_effects_by(creature, item->handle()); })
        .function("__ip_cost_table_int", +[](int32_t prop_type, int32_t cost_value, Value column_val) -> int32_t {
            auto& r = nw::kernel::runtime();
            StringView col = r.get_string_view(column_val.data.hptr);
            auto def = nw::kernel::effects().ip_definition(ItemPropertyType::make(static_cast<uint16_t>(prop_type)));
            if (def && def->cost_table) {
                if (auto val = def->cost_table->get<int>(static_cast<size_t>(cost_value), col)) {
                    return static_cast<int32_t>(*val);
                }
            }
            return 0; })
        .function("__sync_item_propset", +[](nw::ObjectHandle item_h) {
            auto* item = as_item(item_h);
            if (!item) { return; }
            auto& rt = nw::kernel::runtime();
            rt.init_object_propsets(item->handle());
            nwn1::populate_item_propsets(&rt, item); })

        // The End
        .finalize();
}

} // namespace nw::smalls
