#include "../stdlib.hpp"

#include "../../formats/StaticTwoDA.hpp"
#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/Equips.hpp"
#include "../../objects/Inventory.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../objects/Placeable.hpp"
#include "../../objects/Store.hpp"
#include "../../rules/effects.hpp"
#include "../Array.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <utility>

namespace nw::smalls {

namespace {

struct ScriptItemProperty {
    int32_t prop_type;
    int32_t subtype;
    int32_t cost_table;
    int32_t cost_value;
    int32_t param_table;
    int32_t param_value;
    ScriptString tag;
};

struct ScriptBaseItemInfo {
    ScriptString label;
    int32_t name = -1;
    int32_t model_type = 0;
    nw::Resref item_class;
    nw::Resref default_model;
    nw::Resref default_icon;
    int32_t item_property_column = -1;
    int32_t inventory_width = 0;
    int32_t inventory_height = 0;
    int32_t equipable_slots = 0;
    int32_t stack_size = 0;
    bool is_container = false;
};

bool publish_baseitem_info(Value value)
{
    auto& rt = nw::kernel::runtime();
    const TypeID info_type = rt.type_id("core.item.BaseItemInfo");
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || info_type == invalid_type_id
        || array->element_type() != info_type) {
        return false;
    }

    auto& rules = nw::kernel::rules();
    nw::BaseItemArray next{rules.allocator()};
    next.entries.reserve(array->size());
    if (array->size() == 0) {
        rules.baseitems = std::move(next);
        return true;
    }

    size_t valid_count = 0;
    for (size_t i = 0; i < array->size(); ++i) {
        Value element;
        if (!array->get_value(i, element, rt) || element.type_id != info_type) {
            return false;
        }

        const auto source = detail::value_cast<ScriptBaseItemInfo>(&rt, element);
        const auto label = source.label.view(rt);
        if (label.empty()) {
            next.entries.emplace_back();
            continue;
        }

        if (source.model_type < static_cast<int32_t>(nw::ItemModelType::simple)
            || source.model_type > static_cast<int32_t>(nw::ItemModelType::armor)
            || source.name < -1
            || source.item_property_column < -1
            || source.inventory_width < 1
            || source.inventory_width > nw::Inventory::max_columns
            || source.inventory_height < 1
            || source.inventory_height > nw::Inventory::max_rows
            || source.equipable_slots < 0
            || source.stack_size < 1) {
            return false;
        }

        next.entries.push_back(nw::BaseItemInfo{
            .label = nw::String{label},
            .name = static_cast<uint32_t>(source.name),
            .model_type = static_cast<nw::ItemModelType>(source.model_type),
            .item_class = source.item_class,
            .default_model = source.default_model,
            .default_icon = source.default_icon,
            .item_property_column = source.item_property_column,
            .inventory_width = source.inventory_width,
            .inventory_height = source.inventory_height,
            .equipable_slots = source.equipable_slots,
            .stack_size = source.stack_size,
            .is_container = source.is_container,
        });
        ++valid_count;
    }

    if (valid_count == 0) {
        return false;
    }

    rules.baseitems = std::move(next);
    return true;
}

nw::Item* as_item(nw::ObjectHandle obj)
{
    return nw::kernel::objects().get<nw::Item>(obj);
}

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    return nw::kernel::objects().get<nw::Creature>(obj);
}

nw::Inventory* as_inventory(nw::ObjectHandle obj)
{
    auto* owner = nw::kernel::objects().get_object_base(obj);
    if (!owner) {
        return nullptr;
    }
    if (auto* inventory = nw::kernel::objects().components().find_inventory(*owner)) {
        return inventory;
    }
    if (auto* creature = owner->as_creature()) {
        return &creature->inventory();
    }
    if (auto* item = owner->as_item()) {
        return &item->inventory();
    }
    if (auto* placeable = owner->as_placeable()) {
        return &placeable->inventory();
    }
    return nullptr;
}

const nw::ItemPropertyDefinition* item_property_definition(int32_t prop_type)
{
    if (prop_type < 0 || prop_type > 65535) { return nullptr; }
    return nw::kernel::effects().ip_definition(ItemPropertyType::make(static_cast<uint16_t>(prop_type)));
}

const nw::StaticTwoDA* item_property_option_table(int32_t prop_type, int32_t field)
{
    const auto* definition = item_property_definition(prop_type);
    if (!definition) { return nullptr; }
    switch (field) {
    case 0:
        return definition->subtype;
    case 1:
        return definition->param_table;
    case 2:
        return definition->cost_table;
    default:
        return nullptr;
    }
}

int32_t item_property_option_table_index(int32_t prop_type, int32_t field)
{
    constexpr int32_t invalid_table = 255;
    const auto* target = item_property_option_table(prop_type, field);
    if (!target || (field != 1 && field != 2)) { return invalid_table; }
    for (int32_t index = 0; index < invalid_table; ++index) {
        const auto* candidate = field == 1
            ? nw::kernel::effects().ip_param_table(static_cast<size_t>(index))
            : nw::kernel::effects().ip_cost_table(static_cast<size_t>(index));
        if (candidate == target) { return index; }
    }
    return invalid_table;
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

bool read_int_array(Runtime& rt, Value value, nw::Vector<int32_t>& out)
{
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || array->element_type() != rt.int_type()) {
        return false;
    }

    out.clear();
    out.reserve(array->size());
    for (size_t i = 0; i < array->size(); ++i) {
        Value element;
        if (!array->get_value(i, element, rt) || element.type_id != rt.int_type()) {
            out.clear();
            return false;
        }
        out.push_back(element.data.ival);
    }
    return true;
}

Value make_int_array(Runtime& rt, std::span<const int32_t> values)
{
    const HeapPtr array_ptr = rt.create_array_typed(rt.int_type(), values.size());
    auto* array = rt.get_array_typed(array_ptr);
    if (!array) {
        return {};
    }
    for (int32_t value : values) {
        array->append_value(Value::make_int(value), rt);
    }
    return Value::make_heap(array_ptr, rt.heap_.get_header(array_ptr)->type_id);
}

Value make_inventory_item_array(Runtime& rt, const nw::Inventory* inventory,
    size_t maximum_items = std::numeric_limits<size_t>::max())
{
    const TypeID item_type = rt.object_subtype_for_tag(nw::ObjectType::item);
    if (item_type == invalid_type_id) {
        return {};
    }

    const size_t item_count = inventory
        ? std::min(inventory->items.size(), maximum_items)
        : 0;
    const HeapPtr array_ptr = rt.create_array_typed(item_type, item_count);
    auto* items = rt.get_array_typed(array_ptr);
    if (!items) {
        return {};
    }

    if (inventory) {
        for (const auto& entry : inventory->items) {
            if (items->size() >= item_count) { break; }
            if (const auto* item = inventory_item_ptr(entry)) {
                items->append_value(
                    detail::make_value(&rt, item->handle()), rt);
            }
        }
    }

    return Value::make_heap(
        array_ptr, rt.heap_.get_header(array_ptr)->type_id);
}

int32_t inventory_item_count(const nw::Inventory* inventory) noexcept
{
    if (!inventory) { return 0; }
    size_t count = 0;
    for (const auto& entry : inventory->items) {
        count += inventory_item_ptr(entry) ? 1u : 0u;
    }
    return count > static_cast<size_t>(std::numeric_limits<int32_t>::max())
        ? std::numeric_limits<int32_t>::max()
        : static_cast<int32_t>(count);
}

const nw::Inventory* store_inventory(
    nw::ObjectHandle store_h, int32_t category) noexcept
{
    const auto* store = nw::kernel::objects().get<nw::Store>(store_h);
    if (!store) {
        return nullptr;
    }

    const auto& inventory = store->inventory();
    switch (category) {
    case 0:
        return &inventory.armor;
    case 1:
        return &inventory.miscellaneous;
    case 2:
        return &inventory.potions;
    case 3:
        return &inventory.rings;
    case 4:
        return &inventory.weapons;
    default:
        return nullptr;
    }
}

Value make_item_property_array(Runtime& rt, std::span<const nw::ItemProperty> properties)
{
    const TypeID property_type = rt.type_id("core.item.ItemProperty");
    if (property_type == invalid_type_id) {
        return {};
    }

    const HeapPtr array_ptr = rt.create_array_typed(property_type, properties.size());
    auto* array = rt.get_array_typed(array_ptr);
    if (!array) {
        return {};
    }

    for (const auto& property : properties) {
        const ScriptItemProperty value{
            .prop_type = property.type,
            .subtype = property.subtype,
            .cost_table = property.cost_table,
            .cost_value = property.cost_value,
            .param_table = property.param_table,
            .param_value = property.param_value,
            .tag = ScriptString{rt.alloc_string(property.tag)},
        };
        array->append_value(detail::make_value(&rt, value), rt);
    }
    return Value::make_heap(array_ptr, rt.heap_.get_header(array_ptr)->type_id);
}

bool read_item_property_array(
    Runtime& rt, Value value, nw::Vector<nw::ItemProperty>& properties)
{
    const TypeID property_type = rt.type_id("core.item.ItemProperty");
    auto* array = rt.get_array_typed(value.data.hptr);
    if (!array || property_type == invalid_type_id
        || array->element_type() != property_type) {
        return false;
    }

    properties.clear();
    properties.reserve(array->size());
    for (size_t i = 0; i < array->size(); ++i) {
        Value element;
        if (!array->get_value(i, element, rt) || element.type_id != property_type) {
            properties.clear();
            return false;
        }

        const auto source = detail::value_cast<ScriptItemProperty>(&rt, element);
        if (source.prop_type < 0
            || source.prop_type > std::numeric_limits<uint16_t>::max()
            || source.subtype < 0
            || source.subtype > std::numeric_limits<uint16_t>::max()
            || source.cost_table < 0
            || source.cost_table > std::numeric_limits<uint8_t>::max()
            || source.cost_value < 0
            || source.cost_value > std::numeric_limits<uint16_t>::max()
            || source.param_table < 0
            || source.param_table > std::numeric_limits<uint8_t>::max()
            || source.param_value < 0
            || source.param_value > std::numeric_limits<uint8_t>::max()) {
            properties.clear();
            return false;
        }

        properties.push_back(nw::ItemProperty{
            .type = static_cast<uint16_t>(source.prop_type),
            .subtype = static_cast<uint16_t>(source.subtype),
            .cost_table = static_cast<uint8_t>(source.cost_table),
            .cost_value = static_cast<uint16_t>(source.cost_value),
            .param_table = static_cast<uint8_t>(source.param_table),
            .param_value = static_cast<uint8_t>(source.param_value),
            .tag = std::string{source.tag.view(rt)},
        });
    }
    return true;
}

bool read_visual_colors(nw::ObjectHandle item_h,
    std::span<const int32_t> parts,
    std::span<const int32_t> colors,
    nw::Vector<int32_t>& out)
{
    if (!as_item(item_h) || parts.size() != colors.size()) {
        return false;
    }

    auto* visuals = nw::kernel::objects().components().get_or_create_item_visuals(item_h);
    if (!visuals) {
        return false;
    }

    out.clear();
    out.reserve(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
        const int32_t part = parts[i];
        const int32_t color = colors[i];
        if ((part < -1 || part >= int32_t(ObjectItemVisualState::model_part_count))
            || color < 0 || color >= int32_t(ObjectItemVisualState::model_color_count)) {
            out.clear();
            return false;
        }

        const uint8_t value = part == -1
            ? visuals->model_colors[color]
            : visuals->part_colors[size_t(part) * ObjectItemVisualState::model_color_count + size_t(color)];
        out.push_back(value);
    }
    return true;
}

bool write_visual_colors(nw::ObjectHandle item_h,
    std::span<const int32_t> parts,
    std::span<const int32_t> colors,
    std::span<const int32_t> values)
{
    if (!as_item(item_h) || parts.size() != colors.size() || parts.size() != values.size()) {
        return false;
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        if ((parts[i] < -1 || parts[i] >= int32_t(ObjectItemVisualState::model_part_count))
            || colors[i] < 0 || colors[i] >= int32_t(ObjectItemVisualState::model_color_count)
            || values[i] < 0 || values[i] > std::numeric_limits<uint8_t>::max()) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (parts[i] == parts[j] && colors[i] == colors[j]) {
                return false;
            }
        }
    }

    auto* visuals = nw::kernel::objects().components().get_or_create_item_visuals(item_h);
    if (!visuals) {
        return false;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == -1) {
            visuals->model_colors[colors[i]] = static_cast<uint8_t>(values[i]);
        } else {
            const size_t index = size_t(parts[i]) * ObjectItemVisualState::model_color_count
                + size_t(colors[i]);
            visuals->part_colors[index] = static_cast<uint8_t>(values[i]);
        }
    }
    return true;
}

bool read_visual_model_parts(nw::ObjectHandle item_h,
    std::span<const int32_t> parts,
    nw::Vector<int32_t>& out)
{
    if (!as_item(item_h)) {
        return false;
    }

    auto* visuals = nw::kernel::objects().components().get_or_create_item_visuals(item_h);
    if (!visuals) {
        return false;
    }

    out.clear();
    out.reserve(parts.size());
    for (int32_t part : parts) {
        if (part < 0 || part >= int32_t(ObjectItemVisualState::model_part_count)) {
            out.clear();
            return false;
        }
        out.push_back(visuals->model_parts[part]);
    }
    return true;
}

bool write_visual_model_parts(nw::ObjectHandle item_h,
    std::span<const int32_t> parts,
    std::span<const int32_t> values)
{
    if (!as_item(item_h) || parts.size() != values.size()) {
        return false;
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] < 0 || parts[i] >= int32_t(ObjectItemVisualState::model_part_count)
            || values[i] < 0 || values[i] > std::numeric_limits<uint16_t>::max()) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (parts[i] == parts[j]) {
                return false;
            }
        }
    }

    auto* visuals = nw::kernel::objects().components().get_or_create_item_visuals(item_h);
    if (!visuals) {
        return false;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        visuals->model_parts[parts[i]] = static_cast<uint16_t>(values[i]);
    }
    return true;
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
        .field("tag", &ScriptItemProperty::tag)
        .end_struct()
        .native_struct<ScriptBaseItemInfo>("BaseItemInfo")
        .field("label", &ScriptBaseItemInfo::label)
        .field("name", &ScriptBaseItemInfo::name)
        .field("model_type", &ScriptBaseItemInfo::model_type)
        .field("item_class", &ScriptBaseItemInfo::item_class)
        .field("default_model", &ScriptBaseItemInfo::default_model)
        .field("default_icon", &ScriptBaseItemInfo::default_icon)
        .field("item_property_column", &ScriptBaseItemInfo::item_property_column)
        .field("inventory_width", &ScriptBaseItemInfo::inventory_width)
        .field("inventory_height", &ScriptBaseItemInfo::inventory_height)
        .field("equipable_slots", &ScriptBaseItemInfo::equipable_slots)
        .field("stack_size", &ScriptBaseItemInfo::stack_size)
        .field("is_container", &ScriptBaseItemInfo::is_container)
        .end_struct()
        .function("publish_baseitem_info", &publish_baseitem_info)
        .function("apply_item_effect", +[](nw::ObjectHandle creature_h, nw::ObjectHandle item_h, nw::TypedHandle effect_h) -> bool {
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
        .function("remove_item_effects", +[](nw::ObjectHandle creature_h, nw::ObjectHandle item_h) -> int32_t {
            auto* creature = as_creature(creature_h);
            auto* item = as_item(item_h);
            if (!creature || !item) { return 0; }
            return remove_effects_by_creator(creature, item->handle()); })
        .function("ip_cost_table_int", +[](int32_t prop_type, int32_t cost_value, Value column_val) -> int32_t {
            auto& r = nw::kernel::runtime();
            StringView col = r.get_string_view(column_val.data.hptr);
            auto def = item_property_definition(prop_type);
            if (def && def->cost_table && cost_value >= 0) {
                if (auto val = def->cost_table->get<int>(static_cast<size_t>(cost_value), col)) {
                    return static_cast<int32_t>(*val);
                }
            }
            return 0; })
        .function("ip_definition_cost", +[](int32_t prop_type) -> float {
            auto def = item_property_definition(prop_type);
            return def ? def->cost : 0.0f; })
        .function("ip_subtype_cost", +[](int32_t prop_type, int32_t subtype) -> float {
            auto def = item_property_definition(prop_type);
            if (def && def->subtype && subtype >= 0 && static_cast<size_t>(subtype) < def->subtype->rows()) {
                float value = 0.0f;
                def->subtype->get_to(static_cast<size_t>(subtype), "Cost", value, false);
                return value;
            }
            return 0.0f; })
        .function("ip_cost_table_float", +[](int32_t prop_type, int32_t cost_value, Value column_val) -> float {
            auto& r = nw::kernel::runtime();
            StringView col = r.get_string_view(column_val.data.hptr);
            auto def = item_property_definition(prop_type);
            if (def && def->cost_table && cost_value >= 0 && static_cast<size_t>(cost_value) < def->cost_table->rows()) {
                float value = 0.0f;
                def->cost_table->get_to(static_cast<size_t>(cost_value), col, value);
                return value;
            }
            return 0.0f; })
        .function("ip_definition_count", +[]() -> int32_t {
            const size_t count = nw::kernel::effects().ip_definitions().size();
            return count > static_cast<size_t>(std::numeric_limits<int32_t>::max())
                ? std::numeric_limits<int32_t>::max()
                : static_cast<int32_t>(count); })
        .function("ip_definition_name", +[](int32_t prop_type) -> int32_t {
            const auto* definition = item_property_definition(prop_type);
            if (!definition || definition->name == std::numeric_limits<uint32_t>::max()
                || definition->name > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                return -1;
            }
            return static_cast<int32_t>(definition->name); })
        .function("ip_definition_allowed", +[](int32_t prop_type, int32_t item_property_column) -> bool {
            const auto* table = nw::kernel::effects().itemprops();
            if (!item_property_definition(prop_type) || !table || item_property_column < 0
                || static_cast<size_t>(prop_type) >= table->rows()
                || static_cast<size_t>(item_property_column) >= table->columns()) {
                return false;
            }
            int32_t allowed = 0;
            return table->get_to(static_cast<size_t>(prop_type),
                       static_cast<size_t>(item_property_column), allowed)
                && allowed != 0; })
        .function("ip_definition_table", +[](int32_t prop_type, int32_t field) -> int32_t { return item_property_option_table_index(prop_type, field); })
        .function("ip_option_count", +[](int32_t prop_type, int32_t field) -> int32_t {
            const auto* table = item_property_option_table(prop_type, field);
            if (!table) { return 0; }
            const size_t count = table->rows();
            return count > static_cast<size_t>(std::numeric_limits<int32_t>::max())
                ? std::numeric_limits<int32_t>::max()
                : static_cast<int32_t>(count); })
        .function("ip_option_name", +[](int32_t prop_type, int32_t field, int32_t value) -> int32_t {
            const auto* table = item_property_option_table(prop_type, field);
            if (!table || value < 0 || static_cast<size_t>(value) >= table->rows()) {
                return -1;
            }
            int32_t name = -1;
            return table->get_to(static_cast<size_t>(value), "Name", name, false) ? name : -1; })
        .function("set_item_layout", +[](nw::ObjectHandle item_h, int32_t inventory_width, int32_t inventory_height) -> bool {
            if (!as_item(item_h)) {
                return false;
            }
            return nw::kernel::objects().components().set_item_layout(
                item_h, inventory_width, inventory_height); })
        .function("clear_item_layout", +[](nw::ObjectHandle item_h) -> bool {
            if (!as_item(item_h)) {
                return false;
            }
            nw::kernel::objects().components().remove_item_layout(item_h);
            return true; })
        .function("item_properties", +[](nw::ObjectHandle item_h) -> Value {
            auto& runtime = nw::kernel::runtime();
            const auto* properties = nw::kernel::objects().components().find_item_properties(item_h);
            return make_item_property_array(
                runtime, properties ? std::span{properties->entries} : std::span<const nw::ItemProperty>{}); })
        .function("set_item_properties", +[](nw::ObjectHandle item_h, Value properties_value) -> bool {
            if (!as_item(item_h)) {
                return false;
            }

            auto& runtime = nw::kernel::runtime();
            nw::Vector<nw::ItemProperty> properties;
            return read_item_property_array(runtime, properties_value, properties)
                && nw::kernel::objects().components().set_item_properties(item_h, properties); })
        .function("get_visual_color", +[](nw::ObjectHandle item_h, int32_t part, int32_t color) -> int32_t {
            const std::array<int32_t, 1> parts{part};
            const std::array<int32_t, 1> colors{color};
            nw::Vector<int32_t> result;
            return read_visual_colors(item_h, parts, colors, result) ? result[0] : 0; })
        .function("get_visual_colors", +[](nw::ObjectHandle item_h, Value parts_value, Value colors_value) -> Value {
            auto& runtime = nw::kernel::runtime();
            nw::Vector<int32_t> parts;
            nw::Vector<int32_t> colors;
            nw::Vector<int32_t> result;
            if (!read_int_array(runtime, parts_value, parts)
                || !read_int_array(runtime, colors_value, colors)
                || !read_visual_colors(item_h, parts, colors, result)) {
                result.clear();
            }
            return make_int_array(runtime, result); })
        .function("set_visual_colors_raw", +[](nw::ObjectHandle item_h, Value parts_value, Value colors_value, Value values_value) -> bool {
            auto& runtime = nw::kernel::runtime();
            nw::Vector<int32_t> parts;
            nw::Vector<int32_t> colors;
            nw::Vector<int32_t> values;
            return read_int_array(runtime, parts_value, parts)
                && read_int_array(runtime, colors_value, colors)
                && read_int_array(runtime, values_value, values)
                && write_visual_colors(item_h, parts, colors, values); })
        .function("get_visual_model_part", +[](nw::ObjectHandle item_h, int32_t part) -> int32_t {
            const std::array<int32_t, 1> parts{part};
            nw::Vector<int32_t> result;
            return read_visual_model_parts(item_h, parts, result) ? result[0] : 0; })
        .function("get_visual_model_parts", +[](nw::ObjectHandle item_h, Value parts_value) -> Value {
            auto& runtime = nw::kernel::runtime();
            nw::Vector<int32_t> parts;
            nw::Vector<int32_t> result;
            if (!read_int_array(runtime, parts_value, parts)
                || !read_visual_model_parts(item_h, parts, result)) {
                result.clear();
            }
            return make_int_array(runtime, result); })
        .function("set_visual_model_parts_raw", +[](nw::ObjectHandle item_h, Value parts_value, Value values_value) -> bool {
            auto& runtime = nw::kernel::runtime();
            nw::Vector<int32_t> parts;
            nw::Vector<int32_t> values;
            return read_int_array(runtime, parts_value, parts)
                && read_int_array(runtime, values_value, values)
                && write_visual_model_parts(item_h, parts, values); })
        .function("equip_item_in_slot", +[](nw::ObjectHandle creature_h, nw::ObjectHandle item_h, int32_t slot) -> bool { return nw::equip_item_in_slot(
                                                                                                                              as_creature(creature_h), as_item(item_h), static_cast<nw::EquipIndex>(slot)); })
        .function("get_equipped_item", +[](nw::ObjectHandle creature_h, int32_t slot) -> nw::ObjectHandle {
            auto* item = nw::get_equipped_item(
                as_creature(creature_h), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("unequip_item_in_slot", +[](nw::ObjectHandle creature_h, int32_t slot) -> nw::ObjectHandle {
            auto* item = nw::unequip_item_in_slot(
                as_creature(creature_h), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("inventory_add_item", +[](nw::ObjectHandle owner_h, nw::ObjectHandle item_h) -> bool {
            auto* inventory = as_inventory(owner_h);
            auto* item = as_item(item_h);
            return inventory && item && inventory->add_item(item); })
        .function("inventory_remove_item", +[](nw::ObjectHandle owner_h, nw::ObjectHandle item_h) -> bool {
            auto* inventory = as_inventory(owner_h);
            auto* item = as_item(item_h);
            return inventory && item && inventory->remove_item(item); })
        .function("inventory_items", +[](nw::ObjectHandle owner_h) -> Value {
            auto& runtime = nw::kernel::runtime();
            return make_inventory_item_array(runtime, as_inventory(owner_h)); })
        .function("store_inventory_item_count", +[](nw::ObjectHandle store_h, int32_t category) -> int32_t {
            return inventory_item_count(store_inventory(store_h, category)); })
        .function("store_inventory_items", +[](nw::ObjectHandle store_h, int32_t category, int32_t maximum_items) -> Value {
            auto& runtime = nw::kernel::runtime();
            return make_inventory_item_array(
                runtime, store_inventory(store_h, category),
                maximum_items > 0 ? static_cast<size_t>(maximum_items) : 0u); })

        // The End
        .finalize();
}

} // namespace nw::smalls
