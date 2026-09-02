#include "item_editor.hpp"

#include "ui_v1.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <array>
#include <string_view>

namespace nw::toolset {

namespace {

constexpr std::string_view available_list = "item.properties.available";
constexpr std::string_view applied_list = "item.properties.applied";
constexpr std::string_view option_list = "item.properties.options";
constexpr std::string_view model_list = "item.appearance.models";
constexpr int32_t model_axis_model = 0;
constexpr int32_t model_axis_color = 1;

smalls::Value object_value(smalls::Runtime& runtime, ObjectHandle object)
{
    auto value = smalls::Value::make_object(object);
    value.type_id = runtime.object_subtype_for_tag(object.type);
    return value;
}

bool read_int(smalls::Runtime& runtime, const smalls::Value& row,
    std::string_view field, int32_t& output)
{
    if (row.storage != smalls::ValueStorage::heap) {
        return false;
    }
    const auto value = runtime.read_struct_field(
        row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.int_type()) {
        return false;
    }
    output = value.data.ival;
    return true;
}

bool read_bool(smalls::Runtime& runtime, const smalls::Value& row,
    std::string_view field, bool& output)
{
    if (row.storage != smalls::ValueStorage::heap) {
        return false;
    }
    const auto value = runtime.read_struct_field(
        row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.bool_type()) {
        return false;
    }
    output = value.data.bval;
    return true;
}

bool read_string(smalls::Runtime& runtime, const smalls::Value& row,
    std::string_view field, std::string& output)
{
    if (row.storage != smalls::ValueStorage::heap) {
        return false;
    }
    const auto value = runtime.read_struct_field(
        row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.string_type()
        || value.storage != smalls::ValueStorage::heap) {
        return false;
    }
    output = value.data.hptr.value == 0
        ? std::string{}
        : std::string{runtime.get_string_view(value.data.hptr)};
    return true;
}

const smalls::IArray* read_array(smalls::Runtime& runtime,
    const smalls::Value& row, std::string_view field)
{
    if (row.storage != smalls::ValueStorage::heap) {
        return nullptr;
    }
    const auto value = runtime.read_struct_field(
        row.data.hptr, row.type_id, field);
    return value.storage == smalls::ValueStorage::heap
        ? runtime.get_array_typed(value.data.hptr)
        : nullptr;
}

template <typename Row, typename Reader>
bool copy_rows(smalls::Runtime& runtime, const smalls::IArray* input,
    std::vector<Row>& output, Reader reader)
{
    if (!input) {
        return false;
    }
    output.clear();
    output.reserve(input->size());
    for (size_t index = 0; index < input->size(); ++index) {
        smalls::Value value;
        Row row;
        if (!input->get_value(index, value, runtime)
            || !reader(value, row)) {
            output.clear();
            return false;
        }
        output.push_back(std::move(row));
    }
    return true;
}

std::optional<ItemEditorSnapshot> read_snapshot(
    smalls::Runtime& runtime, ObjectHandle object)
{
    if (object.type != ObjectType::item
        || !kernel::objects().valid(object)) {
        return std::nullopt;
    }
    const auto result = runtime.execute_script(
        "nwn1.item", "get_item_editor_snapshot",
        {object_value(runtime, object)});
    if (!result.ok() || result.value.storage != smalls::ValueStorage::heap) {
        return std::nullopt;
    }

    ItemEditorSnapshot snapshot;
    snapshot.object = object;
    if (!read_bool(runtime, result.value,
            "has_inventory", snapshot.has_inventory)
        || !copy_rows(runtime,
            read_array(runtime, result.value, "parts"), snapshot.parts,
            [&](const smalls::Value& value, ItemEditorPart& row) {
                return read_int(runtime, value, "part", row.part)
                    && read_int(runtime, value, "value", row.value)
                    && read_string(runtime, value, "label", row.label)
                    && read_string(runtime, value, "detail", row.detail)
                    && read_bool(runtime, value, "per_part_colors",
                        row.per_part_colors)
                    && read_bool(runtime, value, "split_model_color",
                        row.split_model_color);
            })
        || !copy_rows(runtime,
            read_array(runtime, result.value, "colors"), snapshot.colors,
            [&](const smalls::Value& value, ItemEditorColor& row) {
                return read_int(runtime, value, "part", row.part)
                    && read_int(runtime, value, "color", row.color)
                    && read_int(runtime, value, "value", row.value)
                    && read_int(runtime, value, "stored_value",
                        row.stored_value)
                    && read_int(runtime, value, "palette", row.palette)
                    && read_string(runtime, value, "label", row.label)
                    && read_bool(runtime, value, "inherited", row.inherited);
            })
        || !copy_rows(runtime,
            read_array(runtime, result.value, "available_properties"),
            snapshot.available_properties,
            [&](const smalls::Value& value,
                ItemEditorAvailableProperty& row) {
                return read_int(runtime, value, "prop_type", row.prop_type)
                    && read_int(runtime, value, "subtype", row.subtype)
                    && read_int(runtime, value, "param_table",
                        row.param_table)
                    && read_int(runtime, value, "param_value",
                        row.param_value)
                    && read_int(runtime, value, "cost_table", row.cost_table)
                    && read_int(runtime, value, "cost_value", row.cost_value)
                    && read_string(runtime, value, "label", row.label);
            })
        || !copy_rows(runtime,
            read_array(runtime, result.value, "applied_properties"),
            snapshot.applied_properties,
            [&](const smalls::Value& value,
                ItemEditorAppliedProperty& row) {
                return read_int(runtime, value, "index", row.index)
                    && read_int(runtime, value, "prop_type", row.prop_type)
                    && read_int(runtime, value, "subtype", row.subtype)
                    && read_int(runtime, value, "param_value",
                        row.param_value)
                    && read_int(runtime, value, "cost_value", row.cost_value)
                    && read_string(runtime, value, "label", row.label)
                    && read_string(runtime, value, "subtype_label",
                        row.subtype_label)
                    && read_string(runtime, value, "param_label",
                        row.param_label)
                    && read_string(runtime, value, "cost_label",
                        row.cost_label)
                    && read_bool(runtime, value, "has_subtype",
                        row.has_subtype)
                    && read_bool(runtime, value, "has_param", row.has_param)
                    && read_bool(runtime, value, "has_cost", row.has_cost);
            })) {
        return std::nullopt;
    }
    return snapshot;
}

std::vector<ItemEditorModelOption> read_model_options(
    smalls::Runtime& runtime, ObjectHandle object, int32_t part, int32_t axis)
{
    const auto result = runtime.execute_script(
        "nwn1.item", "get_item_model_editor_options",
        {object_value(runtime, object), smalls::Value::make_int(part),
            smalls::Value::make_int(axis)});
    const auto* array = result.ok() && result.value.storage == smalls::ValueStorage::heap
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    std::vector<ItemEditorModelOption> rows;
    if (!copy_rows(runtime, array, rows,
            [&](const smalls::Value& value, ItemEditorModelOption& row) {
                return read_int(runtime, value, "value", row.value)
                    && read_int(runtime, value, "packed_value",
                        row.packed_value)
                    && read_string(runtime, value, "label", row.label)
                    && read_string(runtime, value, "detail", row.detail);
            })) {
        rows.clear();
    }
    return rows;
}

std::vector<ItemEditorPropertyOption> read_property_options(
    smalls::Runtime& runtime, ObjectHandle object, int32_t index, int32_t field)
{
    const auto result = runtime.execute_script(
        "nwn1.item", "get_item_property_option_rows",
        {object_value(runtime, object), smalls::Value::make_int(index),
            smalls::Value::make_int(field)});
    const auto* array = result.ok() && result.value.storage == smalls::ValueStorage::heap
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    std::vector<ItemEditorPropertyOption> rows;
    if (!copy_rows(runtime, array, rows,
            [&](const smalls::Value& value, ItemEditorPropertyOption& row) {
                return read_int(runtime, value, "value", row.value)
                    && read_string(runtime, value, "label", row.label);
            })) {
        rows.clear();
    }
    return rows;
}

bool selection_matches(const UiListSelection& selection,
    std::string_view list_id, int32_t value, size_t count)
{
    return selection.list_id == list_id && selection.index >= 0
        && static_cast<size_t>(selection.index) < count
        && selection.key == std::to_string(value);
}

} // namespace

std::string_view item_editor_palette_asset(int32_t palette) noexcept
{
    if (palette == 0) { return "mvpal_cloth.png"; }
    if (palette == 1) { return "mvpal_leather.png"; }
    if (palette == 2) { return "mvpal_armor01.png"; }
    return {};
}

bool ItemEditor::ensure_lists(VirtualListHost& host)
{
    if (host_generation_ == host.generation()) {
        return true;
    }
    const UiListConfig config{.row_height = 34, .overscan = 6, .columns = 1};
    if (!host.create(std::string{available_list}, config)
        || !host.create(std::string{applied_list}, config)
        || !host.create(std::string{option_list}, config)
        || !host.create(std::string{model_list}, config)
        || !host.set_title(option_list, "Property Value")
        || !host.set_visible(option_list, false)
        || !host.set_visible(model_list, false)
        || !host.register_refresh_callback("toolset.item_editor.refresh")
        || !host.set_callback(applied_list, UiListEventType::activate,
            "toolset.item_editor.on_applied_activate")
        || !host.set_callback(option_list, UiListEventType::activate,
            "toolset.item_editor.on_option_activate")
        || !host.set_callback(model_list, UiListEventType::activate,
            "toolset.item_editor.on_model_activate")) {
        return false;
    }
    host_generation_ = host.generation();
    return true;
}

bool ItemEditor::clear(VirtualListHost& host)
{
    if (!ensure_lists(host)) { return false; }
    snapshot_ = ItemEditorSnapshot{};
    model_options_.clear();
    property_options_.clear();
    appearance_mode_ = ItemEditorAppearanceMode::main;
    model_part_ = -1;
    model_axis_ = model_axis_model;
    color_part_ = -1;
    color_channel_ = 0;
    property_index_ = -1;
    property_field_ = -1;
    return host.set_items(available_list, {})
        && host.set_items(applied_list, {})
        && host.set_items(option_list, {})
        && host.set_visible(option_list, false)
        && host.set_items(model_list, {})
        && host.set_visible(model_list, false);
}

bool ItemEditor::refresh(smalls::Runtime& runtime,
    ObjectHandle object, VirtualListHost& host)
{
    if (!ensure_lists(host)) { return false; }
    const auto snapshot = read_snapshot(runtime, object);
    if (!snapshot) { return clear(host); }
    if (snapshot_.object != object) {
        appearance_mode_ = ItemEditorAppearanceMode::main;
        model_part_ = -1;
        model_axis_ = model_axis_model;
        color_part_ = -1;
        color_channel_ = 0;
    }
    snapshot_ = *snapshot;

    std::vector<UiListItem> available;
    available.reserve(snapshot_.available_properties.size());
    for (const auto& row : snapshot_.available_properties) {
        available.push_back(UiListItem{
            .key = std::to_string(row.prop_type),
            .cells = {row.label, {}, {}, {}},
            .cell_count = 1,
            .enabled_mask = 1,
        });
    }
    std::vector<UiListItem> applied;
    applied.reserve(snapshot_.applied_properties.size());
    for (const auto& row : snapshot_.applied_properties) {
        uint8_t enabled = 1;
        if (row.has_subtype) { enabled |= 2; }
        if (row.has_param) { enabled |= 4; }
        if (row.has_cost) { enabled |= 8; }
        applied.push_back(UiListItem{
            .key = std::to_string(row.index),
            .cells = {row.label,
                row.has_subtype ? row.subtype_label : "-",
                row.has_param ? row.param_label : "-",
                row.has_cost ? row.cost_label : "-"},
            .cell_count = 4,
            .enabled_mask = enabled,
        });
    }
    if (!host.set_items(available_list, std::move(available))
        || !host.set_items(applied_list, std::move(applied))
        || !close_property_options(host)) {
        return false;
    }
    return appearance_mode_ != ItemEditorAppearanceMode::model
        || refresh_model_options(runtime, host);
}

bool ItemEditor::has_inventory() const noexcept
{
    return snapshot_.object.type == ObjectType::item
        && snapshot_.has_inventory;
}

const ItemEditorPart* ItemEditor::find_part(int32_t part) const
{
    const auto found = std::ranges::find(
        snapshot_.parts, part, &ItemEditorPart::part);
    return found == snapshot_.parts.end() ? nullptr : &*found;
}

const ItemEditorColor* ItemEditor::find_color(
    int32_t part, int32_t color) const
{
    const auto found = std::ranges::find_if(snapshot_.colors,
        [&](const ItemEditorColor& row) {
            return row.part == part && row.color == color;
        });
    return found == snapshot_.colors.end() ? nullptr : &*found;
}

bool ItemEditor::refresh_model_options(
    smalls::Runtime& runtime, VirtualListHost& host)
{
    const auto* part = find_part(model_part_);
    if (!part
        || (!part->split_model_color && model_axis_ != model_axis_model)
        || (model_axis_ != model_axis_model && model_axis_ != model_axis_color)) {
        return false;
    }
    model_options_ = read_model_options(
        runtime, snapshot_.object, model_part_, model_axis_);
    if (model_options_.empty()) { return false; }

    std::vector<UiListItem> items;
    items.reserve(model_options_.size());
    int selected = -1;
    for (size_t index = 0; index < model_options_.size(); ++index) {
        const auto& row = model_options_[index];
        std::string label = row.label;
        if (!row.detail.empty()) {
            label += "  ";
            label += row.detail;
        }
        items.push_back(UiListItem{
            .key = std::to_string(row.value),
            .cells = {std::move(label), {}, {}, {}},
            .cell_count = 1,
            .enabled_mask = 1,
        });
        if (row.packed_value == part->value) {
            selected = static_cast<int>(index);
        }
    }
    std::string title = part->label;
    if (part->split_model_color) {
        title += model_axis_ == model_axis_model ? " Model" : " Color";
    }
    if (!host.set_items(model_list, std::move(items))
        || !host.set_title(model_list, std::move(title))
        || !host.set_visible(model_list, true)) {
        return false;
    }
    if (selected < 0) { return true; }
    return host.set_selected(model_list,
        UiListSelection{
            .list_id = std::string{model_list},
            .key = std::to_string(
                model_options_[static_cast<size_t>(selected)].value),
            .index = selected,
            .cell = -1,
        },
        false);
}

bool ItemEditor::open_model(smalls::Runtime& runtime,
    int32_t part, int32_t axis, VirtualListHost& host)
{
    model_part_ = part;
    model_axis_ = axis;
    if (!refresh_model_options(runtime, host)) {
        model_part_ = -1;
        model_axis_ = model_axis_model;
        return false;
    }
    appearance_mode_ = ItemEditorAppearanceMode::model;
    return true;
}

bool ItemEditor::close_appearance(VirtualListHost& host)
{
    appearance_mode_ = ItemEditorAppearanceMode::main;
    model_part_ = -1;
    model_axis_ = model_axis_model;
    color_part_ = -1;
    color_channel_ = 0;
    model_options_.clear();
    return host.set_items(model_list, {})
        && host.set_visible(model_list, false);
}

bool ItemEditor::open_color(
    int32_t part, int32_t color, VirtualListHost& host)
{
    const auto* row = find_color(part, color);
    if (!row || item_editor_palette_asset(row->palette).empty()) { return false; }
    appearance_mode_ = ItemEditorAppearanceMode::color;
    color_part_ = part;
    color_channel_ = color;
    return host.set_visible(model_list, false);
}

bool ItemEditor::select_color(int32_t color)
{
    if (!find_color(color_part_, color)) { return false; }
    color_channel_ = color;
    return true;
}

std::optional<ItemEditorColorEdit> ItemEditor::color_edit(int32_t value) const
{
    const auto* row = find_color(color_part_, color_channel_);
    if (!row || (value != 255 && (value < 0 || value >= item_editor_palette_cell_count))
        || (value == 255 && color_part_ < 0)) {
        return std::nullopt;
    }
    return ItemEditorColorEdit{color_part_, color_channel_, value};
}

std::optional<ItemEditorModelEdit> ItemEditor::activate_model(
    const UiListSelection& selection) const
{
    if (appearance_mode_ != ItemEditorAppearanceMode::model
        || selection.index < 0
        || static_cast<size_t>(selection.index) >= model_options_.size()) {
        return std::nullopt;
    }
    const auto& option = model_options_[static_cast<size_t>(selection.index)];
    if (!selection_matches(
            selection, model_list, option.value, model_options_.size())) {
        return std::nullopt;
    }
    return ItemEditorModelEdit{model_part_, option.packed_value};
}

std::optional<ItemEditorAvailableProperty>
ItemEditor::selected_available_property(const VirtualListHost& host) const
{
    const auto selection = host.get_selected(available_list);
    if (!selection || selection->index < 0
        || static_cast<size_t>(selection->index)
            >= snapshot_.available_properties.size()) {
        return std::nullopt;
    }
    const auto& row = snapshot_.available_properties[static_cast<size_t>(selection->index)];
    return selection_matches(*selection, available_list, row.prop_type,
               snapshot_.available_properties.size())
        ? std::optional{row}
        : std::nullopt;
}

std::optional<int32_t> ItemEditor::selected_applied_property(
    const VirtualListHost& host) const
{
    const auto selection = host.get_selected(applied_list);
    if (!selection || selection->index < 0
        || static_cast<size_t>(selection->index)
            >= snapshot_.applied_properties.size()) {
        return std::nullopt;
    }
    const auto& row = snapshot_.applied_properties[static_cast<size_t>(selection->index)];
    return selection_matches(*selection, applied_list, row.index,
               snapshot_.applied_properties.size())
        ? std::optional{row.index}
        : std::nullopt;
}

bool ItemEditor::open_property_options(smalls::Runtime& runtime,
    const UiListSelection& selection, VirtualListHost& host)
{
    const int32_t field = selection.cell - 1;
    if (field < 0 || field > 2 || selection.index < 0
        || static_cast<size_t>(selection.index)
            >= snapshot_.applied_properties.size()) {
        return false;
    }
    const auto& row = snapshot_.applied_properties[static_cast<size_t>(selection.index)];
    if (!selection_matches(selection, applied_list, row.index,
            snapshot_.applied_properties.size())
        || (field == 0 && !row.has_subtype)
        || (field == 1 && !row.has_param)
        || (field == 2 && !row.has_cost)) {
        return false;
    }
    property_options_ = read_property_options(
        runtime, snapshot_.object, row.index, field);
    if (property_options_.empty()) { return false; }
    std::vector<UiListItem> items;
    items.reserve(property_options_.size());
    const int32_t selected_value = field == 0 ? row.subtype
        : field == 1                          ? row.param_value
                                              : row.cost_value;
    int selected = -1;
    for (size_t index = 0; index < property_options_.size(); ++index) {
        const auto& option = property_options_[index];
        items.push_back(UiListItem{
            .key = std::to_string(option.value),
            .cells = {option.label, {}, {}, {}},
            .cell_count = 1,
            .enabled_mask = 1,
        });
        if (option.value == selected_value) {
            selected = static_cast<int>(index);
        }
    }
    property_index_ = row.index;
    property_field_ = field;
    static constexpr std::array<std::string_view, 3> titles{
        "Subtype", "Parameter", "Cost"};
    if (!host.set_items(option_list, std::move(items))
        || !host.set_title(option_list,
            std::string{titles[static_cast<size_t>(field)]})
        || !host.set_visible(option_list, true)) {
        return false;
    }
    if (selected < 0) { return true; }
    return host.set_selected(option_list,
        UiListSelection{
            .list_id = std::string{option_list},
            .key = std::to_string(selected_value),
            .index = selected,
            .cell = -1,
        },
        false);
}

bool ItemEditor::close_property_options(VirtualListHost& host)
{
    property_index_ = -1;
    property_field_ = -1;
    property_options_.clear();
    return host.set_items(option_list, {})
        && host.set_visible(option_list, false);
}

std::optional<ItemEditorPropertyValueEdit>
ItemEditor::activate_property_option(
    const UiListSelection& selection) const
{
    if (property_index_ < 0 || property_field_ < 0 || property_field_ > 2
        || selection.index < 0
        || static_cast<size_t>(selection.index) >= property_options_.size()) {
        return std::nullopt;
    }
    const auto& option = property_options_[static_cast<size_t>(selection.index)];
    if (!selection_matches(
            selection, option_list, option.value, property_options_.size())) {
        return std::nullopt;
    }
    return ItemEditorPropertyValueEdit{
        property_index_, property_field_, option.value};
}

bool ItemEditor::select_applied(int32_t index, VirtualListHost& host) const
{
    if (index < 0
        || static_cast<size_t>(index) >= snapshot_.applied_properties.size()) {
        return index < 0;
    }
    const auto& row = snapshot_.applied_properties[static_cast<size_t>(index)];
    return host.set_selected(applied_list,
        UiListSelection{
            .list_id = std::string{applied_list},
            .key = std::to_string(row.index),
            .index = index,
            .cell = -1,
        },
        false);
}

ItemEditorAppearanceInput ItemEditor::appearance_input() const noexcept
{
    return ItemEditorAppearanceInput{
        .object = snapshot_.object,
        .parts = snapshot_.parts,
        .colors = snapshot_.colors,
        .mode = appearance_mode_,
        .color_part = color_part_,
        .color_channel = color_channel_,
    };
}

} // namespace nw::toolset
