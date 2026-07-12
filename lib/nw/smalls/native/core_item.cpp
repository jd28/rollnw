#include "../stdlib.hpp"

#include "../../formats/StaticTwoDA.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../rules/effects.hpp"

namespace nw::smalls {

namespace {

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

const nw::ItemPropertyDefinition* item_property_definition(int32_t prop_type)
{
    if (prop_type < 0 || prop_type > 65535) { return nullptr; }
    return nw::kernel::effects().ip_definition(ItemPropertyType::make(static_cast<uint16_t>(prop_type)));
}

int32_t remove_effects_by_creator(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    if (!obj) { return 0; }

    nw::Vector<nw::Effect*> to_remove;
    to_remove.reserve(obj->effects().size());
    for (const auto& handle : obj->effects()) {
        if (handle.creator == creator) {
            to_remove.push_back(handle.effect);
        }
    }

    return static_cast<int32_t>(nw::kernel::effects().remove_from(obj, to_remove, true));
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
            if (!nw::kernel::effects().apply_to(creature, eff)) {
                nw::kernel::effects().destroy(eff);
                return false;
            }
            return true; })
        .function("__remove_item_effects", +[](nw::ObjectHandle creature_h, nw::ObjectHandle item_h) -> int32_t {
            auto* creature = as_creature(creature_h);
            auto* item = as_item(item_h);
            if (!creature || !item) { return 0; }
            return remove_effects_by_creator(creature, item->handle()); })
        .function("__ip_cost_table_int", +[](int32_t prop_type, int32_t cost_value, Value column_val) -> int32_t {
            auto& r = nw::kernel::runtime();
            StringView col = r.get_string_view(column_val.data.hptr);
            auto def = item_property_definition(prop_type);
            if (def && def->cost_table && cost_value >= 0) {
                if (auto val = def->cost_table->get<int>(static_cast<size_t>(cost_value), col)) {
                    return static_cast<int32_t>(*val);
                }
            }
            return 0; })
        .function("__ip_definition_cost", +[](int32_t prop_type) -> float {
            auto def = item_property_definition(prop_type);
            return def ? def->cost : 0.0f; })
        .function("__ip_subtype_cost", +[](int32_t prop_type, int32_t subtype) -> float {
            auto def = item_property_definition(prop_type);
            if (def && def->subtype && subtype >= 0 && static_cast<size_t>(subtype) < def->subtype->rows()) {
                float value = 0.0f;
                def->subtype->get_to(static_cast<size_t>(subtype), "Cost", value, false);
                return value;
            }
            return 0.0f; })
        .function("__ip_cost_table_float", +[](int32_t prop_type, int32_t cost_value, Value column_val) -> float {
            auto& r = nw::kernel::runtime();
            StringView col = r.get_string_view(column_val.data.hptr);
            auto def = item_property_definition(prop_type);
            if (def && def->cost_table && cost_value >= 0 && static_cast<size_t>(cost_value) < def->cost_table->rows()) {
                float value = 0.0f;
                def->cost_table->get_to(static_cast<size_t>(cost_value), col, value);
                return value;
            }
            return 0.0f; })
        .function("__set_item_layout", +[](nw::ObjectHandle item_h, int32_t inventory_width, int32_t inventory_height) -> bool {
            if (!as_item(item_h)) {
                return false;
            }
            return nw::kernel::objects().components().set_item_layout(
                item_h, inventory_width, inventory_height); })
        .function("__clear_item_layout", +[](nw::ObjectHandle item_h) -> bool {
            if (!as_item(item_h)) {
                return false;
            }
            nw::kernel::objects().components().remove_item_layout(item_h);
            return true; })

        // The End
        .finalize();
}

} // namespace nw::smalls
