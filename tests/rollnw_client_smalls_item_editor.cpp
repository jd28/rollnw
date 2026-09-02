#include <gtest/gtest.h>

#include "../tools/client/object_edits.hpp"
#include "../tools/client/workspace.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/rules/items.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <array>
#include <limits>
#include <optional>
#include <span>

namespace nwk = nw::kernel;

namespace {

nw::smalls::Value object_value(
    nw::smalls::Runtime& runtime, nw::ObjectHandle object)
{
    auto value = nw::smalls::Value::make_object(object);
    value.type_id = runtime.object_subtype_for_tag(object.type);
    return value;
}

std::optional<int32_t> read_int_field(nw::smalls::Runtime& runtime,
    const nw::smalls::Value& row,
    std::string_view field)
{
    if (row.storage != nw::smalls::ValueStorage::heap) {
        return std::nullopt;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.int_type()) {
        return std::nullopt;
    }
    return value.data.ival;
}

std::optional<bool> read_bool_field(nw::smalls::Runtime& runtime,
    const nw::smalls::Value& row,
    std::string_view field)
{
    if (row.storage != nw::smalls::ValueStorage::heap) {
        return std::nullopt;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.bool_type()) {
        return std::nullopt;
    }
    return value.data.bval;
}

std::optional<std::string> read_string_field(nw::smalls::Runtime& runtime,
    const nw::smalls::Value& row,
    std::string_view field)
{
    if (row.storage != nw::smalls::ValueStorage::heap) {
        return std::nullopt;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.string_type()
        || value.storage != nw::smalls::ValueStorage::heap) {
        return std::nullopt;
    }
    return value.data.hptr.value == 0
        ? std::optional{std::string{}}
        : std::optional{std::string{runtime.get_string_view(value.data.hptr)}};
}

std::optional<std::string> row_value_by_label(nw::smalls::Runtime& runtime,
    const nw::smalls::IArray& rows,
    std::string_view label)
{
    for (size_t index = 0; index < rows.size(); ++index) {
        nw::smalls::Value row;
        if (!rows.get_value(index, row, runtime)) {
            return std::nullopt;
        }
        const auto row_label = read_string_field(runtime, row, "label");
        if (row_label && *row_label == label) {
            return read_string_field(runtime, row, "value");
        }
    }
    return std::nullopt;
}

const nw::smalls::IArray* item_rows(nw::smalls::Runtime& runtime,
    nw::ObjectHandle item,
    std::string_view function,
    std::optional<int32_t> part = std::nullopt,
    std::optional<int32_t> axis = std::nullopt)
{
    nw::Vector<nw::smalls::Value> args{object_value(runtime, item)};
    if (part) {
        args.push_back(nw::smalls::Value::make_int(*part));
    }
    if (axis) {
        args.push_back(nw::smalls::Value::make_int(*axis));
    }
    const auto result = runtime.execute_script("nwn1.item", function, args);
    return result.ok() && result.value.storage == nw::smalls::ValueStorage::heap
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
}

std::optional<bool> item_bool(nw::smalls::Runtime& runtime,
    nw::ObjectHandle item,
    std::string_view function)
{
    const auto result = runtime.execute_script(
        "nwn1.item", function, {object_value(runtime, item)});
    return result.ok() && result.value.type_id == runtime.bool_type()
        ? std::optional{result.value.data.bval}
        : std::nullopt;
}

bool write_item_stats_int(nw::smalls::Runtime& runtime,
    nw::ObjectHandle item,
    std::string_view field,
    int32_t value)
{
    const auto type = runtime.type_id("nwn1.propsets.ItemStats", false);
    const auto propset = runtime.find_propset_ref(type, item);
    const auto* definition = runtime.get_struct_def(type);
    if (propset.type_id == nw::smalls::invalid_type_id || !definition) {
        return false;
    }
    const uint32_t field_index = definition->field_index(field);
    return field_index != UINT32_MAX
        && runtime.write_value_field_at_offset(propset,
            definition->fields[field_index].offset,
            runtime.int_type(),
            nw::smalls::Value::make_int(value));
}

bool set_item_property_records(nw::ObjectHandle item,
    std::span<const nw::toolset::ItemPropertyRecord> records)
{
    nw::Vector<nw::ItemProperty> properties;
    properties.reserve(records.size());
    for (const auto& record : records) {
        if (record.prop_type < 0 || record.prop_type > UINT16_MAX
            || record.subtype < 0 || record.subtype > UINT16_MAX
            || record.cost_table < 0 || record.cost_table > UINT8_MAX
            || record.cost_value < 0 || record.cost_value > UINT16_MAX
            || record.param_table < 0 || record.param_table > UINT8_MAX
            || record.param_value < 0 || record.param_value > UINT8_MAX) {
            return false;
        }
        properties.push_back({
            .type = static_cast<uint16_t>(record.prop_type),
            .subtype = static_cast<uint16_t>(record.subtype),
            .cost_table = static_cast<uint8_t>(record.cost_table),
            .cost_value = static_cast<uint16_t>(record.cost_value),
            .param_table = static_cast<uint8_t>(record.param_table),
            .param_value = static_cast<uint8_t>(record.param_value),
            .tag = record.tag,
        });
    }
    return nwk::objects().components().set_item_properties(item, properties);
}

struct ItemPartValue {
    int32_t part = -1;
    int32_t value = 0;
};

std::optional<ItemPartValue> first_item_part(
    nw::smalls::Runtime& runtime, nw::ObjectHandle item)
{
    const auto* rows = item_rows(runtime, item, "get_item_editor_parts");
    nw::smalls::Value row;
    if (!rows || rows->size() == 0 || !rows->get_value(0, row, runtime)) {
        return std::nullopt;
    }
    const auto part = read_int_field(runtime, row, "part");
    const auto value = read_int_field(runtime, row, "value");
    return part && value
        ? std::optional{ItemPartValue{*part, *value}}
        : std::nullopt;
}

std::optional<int32_t> first_model_replacement(nw::smalls::Runtime& runtime,
    nw::ObjectHandle item,
    ItemPartValue current)
{
    const auto* rows = item_rows(
        runtime, item, "get_item_model_editor_options", current.part, 0);
    if (!rows) {
        return std::nullopt;
    }
    for (size_t index = 0; index < rows->size(); ++index) {
        nw::smalls::Value row;
        if (!rows->get_value(index, row, runtime)) {
            return std::nullopt;
        }
        const auto packed_value = read_int_field(runtime, row, "packed_value");
        if (!packed_value) {
            return std::nullopt;
        }
        if (*packed_value != current.value) {
            return packed_value;
        }
    }
    return std::nullopt;
}

std::optional<nw::toolset::ItemPropertyRecord> first_available_property(
    nw::smalls::Runtime& runtime, nw::ObjectHandle item)
{
    const auto* rows = item_rows(
        runtime, item, "get_available_item_property_rows");
    if (!rows || rows->size() == 0) {
        return std::nullopt;
    }

    nw::smalls::Value row;
    if (!rows->get_value(0, row, runtime)) {
        return std::nullopt;
    }
    nw::toolset::ItemPropertyRecord record;
    const auto prop_type = read_int_field(runtime, row, "prop_type");
    const auto subtype = read_int_field(runtime, row, "subtype");
    const auto cost_table = read_int_field(runtime, row, "cost_table");
    const auto cost_value = read_int_field(runtime, row, "cost_value");
    const auto param_table = read_int_field(runtime, row, "param_table");
    const auto param_value = read_int_field(runtime, row, "param_value");
    if (!prop_type || !subtype || !cost_table || !cost_value
        || !param_table || !param_value) {
        return std::nullopt;
    }
    record = {*prop_type, *subtype, *cost_table, *cost_value,
        *param_table, *param_value};
    return record;
}

} // namespace

TEST(ClientSmallsItemEditor, GeneralRowsReadTheLiveItem)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    const auto* rows = item_rows(
        runtime, item->handle(), "get_item_editor_general_rows");
    ASSERT_NE(rows, nullptr);
    ASSERT_EQ(rows->size(), 9);

    EXPECT_EQ(row_value_by_label(runtime, *rows, "Name"),
        nwk::strings().get(item->name));
    EXPECT_EQ(row_value_by_label(runtime, *rows, "Tag"), item->tag.view());
    EXPECT_EQ(row_value_by_label(runtime, *rows, "Resref"), item->resref.view());
    const auto base_item = row_value_by_label(runtime, *rows, "Base Item Type");
    ASSERT_TRUE(base_item);
    EXPECT_FALSE(base_item->empty());
    EXPECT_FALSE(base_item->starts_with("Bad Strref"));
    EXPECT_EQ(row_value_by_label(runtime, *rows, "Comments"), item->comment);
}

TEST(ClientSmallsItemEditor, GeneralRowsRejectStaleItems)
{
    auto& runtime = nwk::runtime();
    nw::ObjectHandle stale;
    stale.type = nw::ObjectType::item;
    const auto* rows = item_rows(
        runtime, stale, "get_item_editor_general_rows");
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->size(), 0);
}

TEST(ClientSmallsItemEditor, InventoryEligibilityComesFromNativeBaseItemData)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    ASSERT_EQ(item_bool(runtime, item->handle(), "item_editor_has_inventory"), false);
    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 66));
    EXPECT_EQ(item_bool(runtime, item->handle(), "item_editor_has_inventory"), true);
    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", -1));
    EXPECT_EQ(item_bool(runtime, item->handle(), "item_editor_has_inventory"), false);
}

TEST(ClientSmallsItemEditor, NativeBaseItemRowsExposeAvailableProperties)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 13));
    const auto* rows = item_rows(
        runtime, item->handle(), "get_available_item_property_rows");
    ASSERT_NE(rows, nullptr);
    EXPECT_GT(rows->size(), 0);
    bool found_damage_bonus = false;
    for (size_t index = 0; index < rows->size(); ++index) {
        nw::smalls::Value row;
        ASSERT_TRUE(rows->get_value(index, row, runtime));
        const auto prop_type = read_int_field(runtime, row, "prop_type");
        const auto label = read_string_field(runtime, row, "label");
        ASSERT_TRUE(prop_type);
        ASSERT_TRUE(label);
        EXPECT_FALSE(label->empty());
        EXPECT_FALSE(label->starts_with("Property "));
        found_damage_bonus |= *prop_type == 16;
    }
    EXPECT_TRUE(found_damage_bonus);
}

TEST(ClientSmallsItemEditor, AppliedUnknownPropertyRemainsRemovable)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    const nw::toolset::ItemPropertyRecord property{
        31, 0, 255, 0, 255, 255};
    ASSERT_TRUE(set_item_property_records(
        item->handle(), std::span{&property, size_t{1}}));

    const auto* rows = item_rows(
        runtime, item->handle(), "get_applied_item_property_rows");
    ASSERT_NE(rows, nullptr);
    ASSERT_EQ(rows->size(), 1);
    nw::smalls::Value row;
    ASSERT_TRUE(rows->get_value(0, row, runtime));
    EXPECT_EQ(read_string_field(runtime, row, "label"),
        "Unknown item property (31)");
}

TEST(ClientSmallsItemEditor, PreparesFlatVisualBatches)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 16));
    const auto part = first_item_part(runtime, item->handle());
    ASSERT_TRUE(part);
    const std::array parts{part->part};
    const std::array values{part->value};
    const auto model = nw::toolset::make_item_model_part_edits(
        runtime, item->handle(), parts, values);
    ASSERT_TRUE(model);
    ASSERT_EQ(model->patches.size(), 1);
    EXPECT_EQ(model->patches[0].before, part->value);
    EXPECT_EQ(model->patches[0].after, part->value);

    const auto* color_rows = item_rows(
        runtime, item->handle(), "get_item_editor_color_rows");
    nw::smalls::Value color_row;
    ASSERT_NE(color_rows, nullptr);
    ASSERT_GT(color_rows->size(), 0);
    ASSERT_TRUE(color_rows->get_value(0, color_row, runtime));
    const auto color_part = read_int_field(runtime, color_row, "part");
    const auto color = read_int_field(runtime, color_row, "color");
    const auto stored = read_int_field(runtime, color_row, "stored_value");
    ASSERT_TRUE(color_part);
    ASSERT_TRUE(color);
    ASSERT_TRUE(stored);
    const std::array color_parts{*color_part};
    const std::array colors{*color};
    const std::array color_values{*stored};
    const auto color_batch = nw::toolset::make_item_color_edits(
        runtime, item->handle(), color_parts, colors, color_values);
    ASSERT_TRUE(color_batch);
    ASSERT_EQ(color_batch->patches.size(), 1);
    EXPECT_EQ(color_batch->patches[0].before, *stored);
}

TEST(ClientSmallsItemEditor, CompositeModelAndColorAxesUsePackedTargets)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 13));
    const auto* parts = item_rows(
        runtime, item->handle(), "get_item_editor_parts");
    ASSERT_NE(parts, nullptr);
    ASSERT_EQ(parts->size(), 3);
    const auto current = first_item_part(runtime, item->handle());
    ASSERT_TRUE(current);
    nw::smalls::Value part_row;
    ASSERT_TRUE(parts->get_value(0, part_row, runtime));
    EXPECT_EQ(read_bool_field(runtime, part_row, "split_model_color"), true);
    const auto* model_rows = item_rows(runtime, item->handle(),
        "get_item_model_editor_options", current->part, 0);
    ASSERT_NE(model_rows, nullptr);
    ASSERT_GT(model_rows->size(), 1);

    bool found_current = false;
    std::optional<int32_t> replacement;
    for (size_t index = 0; index < model_rows->size(); ++index) {
        nw::smalls::Value row;
        ASSERT_TRUE(model_rows->get_value(index, row, runtime));
        const auto value = read_int_field(runtime, row, "value");
        const auto packed_value = read_int_field(runtime, row, "packed_value");
        const auto label = read_string_field(runtime, row, "label");
        const auto detail = read_string_field(runtime, row, "detail");
        const auto icon = read_string_field(runtime, row, "icon");
        ASSERT_TRUE(value);
        ASSERT_TRUE(packed_value);
        ASSERT_TRUE(label);
        ASSERT_TRUE(detail);
        EXPECT_FALSE(icon);
        EXPECT_EQ(*packed_value / 10, *value);
        if (*value == 0) {
            EXPECT_EQ(*label, "None");
        } else {
            EXPECT_EQ(*label, "Model " + std::to_string(*value));
            EXPECT_FALSE(detail->empty());
        }
        found_current |= *packed_value == current->value;
        if (*packed_value != current->value && !replacement) {
            EXPECT_GT(*value, 0);
            EXPECT_GT(*packed_value % 10, 0);
            replacement = *packed_value;
        }
    }

    EXPECT_TRUE(found_current);
    ASSERT_TRUE(replacement);
    const std::array edited_parts{current->part};
    const std::array edited_values{*replacement};
    const auto edit = nw::toolset::make_item_model_part_edits(
        runtime, item->handle(), edited_parts, edited_values);
    ASSERT_TRUE(edit);
    ASSERT_TRUE(nw::toolset::apply_object_edits(runtime, *edit,
        nw::toolset::ObjectEditDirection::forward)
            .ok());

    const auto* color_rows = item_rows(runtime, item->handle(),
        "get_item_model_editor_options", current->part, 1);
    ASSERT_NE(color_rows, nullptr);
    ASSERT_GT(color_rows->size(), 0);
    found_current = false;
    for (size_t index = 0; index < color_rows->size(); ++index) {
        nw::smalls::Value row;
        ASSERT_TRUE(color_rows->get_value(index, row, runtime));
        const auto value = read_int_field(runtime, row, "value");
        const auto packed_value = read_int_field(runtime, row, "packed_value");
        const auto label = read_string_field(runtime, row, "label");
        const auto detail = read_string_field(runtime, row, "detail");
        ASSERT_TRUE(value);
        ASSERT_TRUE(packed_value);
        ASSERT_TRUE(label);
        ASSERT_TRUE(detail);
        EXPECT_EQ(*packed_value / 10, *replacement / 10);
        EXPECT_EQ(*packed_value % 10, *value);
        EXPECT_GT(*value, 0);
        EXPECT_EQ(*label, "Variation " + std::to_string(*value));
        EXPECT_FALSE(detail->empty());
        found_current |= *packed_value == *replacement;
    }
    EXPECT_TRUE(found_current);

    const auto* colors = item_rows(
        runtime, item->handle(), "get_item_editor_color_rows");
    ASSERT_NE(colors, nullptr);
    EXPECT_EQ(colors->size(), 0);
    EXPECT_TRUE(nw::toolset::apply_object_edits(runtime, *edit,
        nw::toolset::ObjectEditDirection::inverse)
            .ok());
}

TEST(ClientSmallsItemEditor, CompositeWaraxeOptionsKeepCurrentPackedModelVisible)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 108));
    auto* visuals = nwk::objects().components().get_or_create_item_visuals(
        item->handle());
    ASSERT_NE(visuals, nullptr);
    visuals->model_parts[2] = 83;

    const auto* rows = item_rows(runtime, item->handle(),
        "get_item_model_editor_options", 2, 0);
    ASSERT_NE(rows, nullptr);
    ASSERT_GT(rows->size(), 0);

    bool found_current = false;
    for (size_t index = 0; index < rows->size(); ++index) {
        nw::smalls::Value row;
        ASSERT_TRUE(rows->get_value(index, row, runtime));
        const auto value = read_int_field(runtime, row, "value");
        const auto packed_value = read_int_field(runtime, row, "packed_value");
        const auto label = read_string_field(runtime, row, "label");
        const auto detail = read_string_field(runtime, row, "detail");
        ASSERT_TRUE(value);
        ASSERT_TRUE(packed_value);
        ASSERT_TRUE(label);
        ASSERT_TRUE(detail);
        EXPECT_FALSE(label->empty());
        if (*packed_value == 83) {
            EXPECT_EQ(*value, 8);
            EXPECT_EQ(*label, "Model 8");
            EXPECT_FALSE(detail->empty());
            found_current = true;
        }
    }
    EXPECT_TRUE(found_current);
}

TEST(ClientSmallsItemEditor, ColorRowsFollowBaseItemVisualShape)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    const auto color_count = [&]() {
        const auto* rows = item_rows(
            runtime, item->handle(), "get_item_editor_color_rows");
        EXPECT_NE(rows, nullptr);
        return rows ? rows->size() : 0;
    };

    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 24));
    EXPECT_EQ(color_count(), 0);

    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 13));
    EXPECT_EQ(color_count(), 0);

    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 17));
    const auto* layered_parts = item_rows(
        runtime, item->handle(), "get_item_editor_parts");
    ASSERT_NE(layered_parts, nullptr);
    ASSERT_EQ(layered_parts->size(), 1);
    nw::smalls::Value layered_part;
    ASSERT_TRUE(layered_parts->get_value(0, layered_part, runtime));
    EXPECT_EQ(read_bool_field(runtime, layered_part, "per_part_colors"), true);

    const auto* layered_colors = item_rows(
        runtime, item->handle(), "get_item_editor_color_rows");
    ASSERT_NE(layered_colors, nullptr);
    ASSERT_EQ(layered_colors->size(), 6);
    for (size_t index = 0; index < layered_colors->size(); ++index) {
        nw::smalls::Value row;
        ASSERT_TRUE(layered_colors->get_value(index, row, runtime));
        EXPECT_EQ(read_int_field(runtime, row, "part"), 0);
    }

    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 16));
    EXPECT_EQ(color_count(), 19 * 6);
}

TEST(ClientSmallsItemEditor, RejectsInvalidObjectsAndUnavailableParts)
{
    auto& runtime = nwk::runtime();
    const std::array parts{0};
    const std::array values{1};
    EXPECT_FALSE(nw::toolset::make_item_model_part_edits(
        runtime, nw::ObjectHandle{}, parts, values));

    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);
    const std::array unavailable_parts{18};
    EXPECT_FALSE(nw::toolset::make_item_model_part_edits(
        runtime, item->handle(), unavailable_parts, values));
}

TEST(ClientSmallsItemEditor, VisualEditsRoundTripThroughUndo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    const auto current = first_item_part(runtime, item->handle());
    ASSERT_TRUE(current);
    const auto replacement = first_model_replacement(
        runtime, item->handle(), *current);
    ASSERT_TRUE(replacement);
    const std::array parts{current->part};
    const std::array values{*replacement};
    auto visual = nw::toolset::make_item_model_part_edits(
        runtime, item->handle(), parts, values);
    ASSERT_TRUE(visual);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:item", "Item", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto visual_result = nw::toolset::commit_object_edits(
        std::move(*visual), "Set Item model part", context);
    ASSERT_TRUE(visual_result.ok()) << visual_result.message;
    ASSERT_TRUE(visual_result.undo_action);
    workspace.push_undo(*visual_result.undo_action);

    const std::array original_values{current->value};
    auto inverse = nw::toolset::make_item_model_part_edits(
        runtime, item->handle(), parts, original_values);
    ASSERT_TRUE(inverse);
    EXPECT_EQ(inverse->patches[0].before, *replacement);
    ASSERT_TRUE(workspace.undo(context).ok());
    inverse = nw::toolset::make_item_model_part_edits(
        runtime, item->handle(), parts, values);
    ASSERT_TRUE(inverse);
    EXPECT_EQ(inverse->patches[0].before, current->value);
    ASSERT_TRUE(workspace.redo(context).ok());
    inverse = nw::toolset::make_item_model_part_edits(
        runtime, item->handle(), parts, original_values);
    ASSERT_TRUE(inverse);
    EXPECT_EQ(inverse->patches[0].before, *replacement);
}

TEST(ClientSmallsItemEditor, LayeredColorEditsRoundTripThroughOpaqueSmallsKeys)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    ASSERT_TRUE(write_item_stats_int(runtime, item->handle(), "base_item", 17));
    const auto* rows = item_rows(
        runtime, item->handle(), "get_item_editor_color_rows");
    nw::smalls::Value row;
    ASSERT_NE(rows, nullptr);
    ASSERT_GT(rows->size(), 0);
    ASSERT_TRUE(rows->get_value(0, row, runtime));
    const auto part = read_int_field(runtime, row, "part");
    const auto color = read_int_field(runtime, row, "color");
    const auto stored = read_int_field(runtime, row, "stored_value");
    ASSERT_TRUE(part);
    ASSERT_TRUE(color);
    ASSERT_TRUE(stored);
    EXPECT_EQ(*part, 0);
    const int32_t replacement = *stored == 0 ? 1 : 0;
    const std::array parts{*part};
    const std::array colors{*color};
    const std::array values{replacement};
    auto batch = nw::toolset::make_item_color_edits(
        runtime, item->handle(), parts, colors, values);
    ASSERT_TRUE(batch);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:item", "Item", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto result = nw::toolset::commit_object_edits(
        std::move(*batch), "Set Item color", context);
    ASSERT_TRUE(result.ok()) << result.message;
    ASSERT_TRUE(result.undo_action);
    workspace.push_undo(*result.undo_action);

    const std::array original{*stored};
    auto inverse = nw::toolset::make_item_color_edits(
        runtime, item->handle(), parts, colors, original);
    ASSERT_TRUE(inverse);
    EXPECT_EQ(inverse->patches[0].before, replacement);
    ASSERT_TRUE(workspace.undo(context).ok());
    inverse = nw::toolset::make_item_color_edits(
        runtime, item->handle(), parts, colors, values);
    ASSERT_TRUE(inverse);
    EXPECT_EQ(inverse->patches[0].before, *stored);
    ASSERT_TRUE(workspace.redo(context).ok());

    auto invalid = *inverse;
    invalid.patches[0].key = static_cast<uint32_t>(
        std::numeric_limits<int32_t>::max());
    const auto rejected = nw::toolset::apply_object_edits(
        runtime, invalid, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(rejected.status, nw::toolset::ObjectEditStatus::invalid_batch);
}

TEST(ClientSmallsItemEditor, PropertyEditsRoundTripThroughUndo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    const auto before = nw::toolset::snapshot_item_property_records(
        runtime, item->handle());
    const auto property = first_available_property(runtime, item->handle());
    ASSERT_TRUE(before);
    ASSERT_TRUE(property);

    const nw::toolset::ItemPropertyEditBatch invalid{
        .item = item->handle(),
        .kind = nw::toolset::ItemPropertyEditKind::insert,
        .rows = {{
            .index = static_cast<int32_t>(before->size()),
            .after = {},
        }},
    };
    const auto rejected = nw::toolset::apply_item_property_edits(
        runtime, invalid, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(rejected.status, nw::toolset::ObjectEditStatus::failed);
    EXPECT_EQ(nw::toolset::snapshot_item_property_records(runtime, item->handle()),
        before);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:item", "Item", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    nw::toolset::ItemPropertyEditBatch batch{
        .item = item->handle(),
        .kind = nw::toolset::ItemPropertyEditKind::insert,
        .rows = {{
            .index = static_cast<int32_t>(before->size()),
            .after = *property,
        }},
    };
    auto result = nw::toolset::commit_item_property_edits(
        std::move(batch), "Add Item property", context);
    ASSERT_TRUE(result.ok()) << result.message;
    ASSERT_TRUE(result.undo_action);
    workspace.push_undo(*result.undo_action);

    auto current = nw::toolset::snapshot_item_property_records(runtime, item->handle());
    ASSERT_TRUE(current);
    EXPECT_EQ(current->size(), before->size() + 1);
    ASSERT_TRUE(workspace.undo(context).ok());
    current = nw::toolset::snapshot_item_property_records(runtime, item->handle());
    ASSERT_TRUE(current);
    EXPECT_EQ(current->size(), before->size());
    ASSERT_TRUE(workspace.redo(context).ok());
    current = nw::toolset::snapshot_item_property_records(runtime, item->handle());
    ASSERT_TRUE(current);
    EXPECT_EQ(current->size(), before->size() + 1);
}

TEST(ClientSmallsItemEditor, RemovingPropertyPreservesEveryRemainingRecord)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    auto& runtime = nwk::runtime();
    const auto initial = nw::toolset::snapshot_item_property_records(
        runtime, item->handle());
    ASSERT_TRUE(initial);
    ASSERT_FALSE(initial->empty());

    auto tagged_initial = *initial;
    tagged_initial.front().tag = "remove-undo-tag";
    ASSERT_TRUE(set_item_property_records(item->handle(), tagged_initial));

    const nw::toolset::ItemPropertyEditBatch setup{
        .item = item->handle(),
        .kind = nw::toolset::ItemPropertyEditKind::insert,
        .rows = {
            {
                .index = static_cast<int32_t>(tagged_initial.size()),
                .after = tagged_initial.front(),
            },
            {
                .index = static_cast<int32_t>(tagged_initial.size() + 1),
                .after = tagged_initial.front(),
            },
        },
    };
    const auto inserted = nw::toolset::apply_item_property_edits(
        runtime, setup, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(inserted.ok()) << inserted.diagnostic;

    const auto before = nw::toolset::snapshot_item_property_records(
        runtime, item->handle());
    ASSERT_TRUE(before);
    ASSERT_GT(before->size(), 2);

    constexpr int32_t removed_index = 0;
    nw::toolset::ItemPropertyEditBatch batch{
        .item = item->handle(),
        .kind = nw::toolset::ItemPropertyEditKind::remove,
        .rows = {{
            .index = removed_index,
            .before = (*before)[removed_index],
        }},
    };
    const auto removed = nw::toolset::apply_item_property_edits(
        runtime, batch, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(removed.ok()) << removed.diagnostic;

    auto expected = *before;
    expected.erase(expected.begin() + removed_index);
    EXPECT_EQ(nw::toolset::snapshot_item_property_records(
                  runtime, item->handle()),
        expected);

    const auto* rows = item_rows(
        runtime, item->handle(), "get_applied_item_property_rows");
    ASSERT_NE(rows, nullptr);
    ASSERT_EQ(rows->size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index) {
        nw::smalls::Value row;
        ASSERT_TRUE(rows->get_value(index, row, runtime));
        EXPECT_EQ(read_int_field(runtime, row, "index"), index);
        EXPECT_EQ(read_int_field(runtime, row, "prop_type"),
            expected[index].prop_type);
        EXPECT_EQ(read_int_field(runtime, row, "subtype"),
            expected[index].subtype);
        EXPECT_EQ(read_int_field(runtime, row, "param_table"),
            expected[index].param_table);
        EXPECT_EQ(read_int_field(runtime, row, "param_value"),
            expected[index].param_value);
        EXPECT_EQ(read_int_field(runtime, row, "cost_table"),
            expected[index].cost_table);
        EXPECT_EQ(read_int_field(runtime, row, "cost_value"),
            expected[index].cost_value);
        EXPECT_EQ(read_string_field(runtime, row, "tag"),
            expected[index].tag);
    }

    const auto restored = nw::toolset::apply_item_property_edits(
        runtime, batch, nw::toolset::ObjectEditDirection::inverse);
    ASSERT_TRUE(restored.ok()) << restored.diagnostic;
    EXPECT_EQ(nw::toolset::snapshot_item_property_records(
                  runtime, item->handle()),
        before);
}

TEST(ClientSmallsItemEditor, ComponentJsonPreservesItemPropertyRecords)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);

    const std::vector<nw::toolset::ItemPropertyRecord> expected{
        {16, 0, 4, 11, 255, 0},
        {16, 10, 4, 10, 255, 0},
        {16, 5, 4, 7, 255, 0},
        {16, 1, 4, 11, 255, 0},
        {6, 0, 2, 6, 255, 0},
        {43, 0, 0, 0, 255, 0},
        {74, 0, 4, 10, 255, 0},
        {83, 6, 0, 0, 255, 0},
    };
    auto& runtime = nwk::runtime();
    ASSERT_TRUE(set_item_property_records(item->handle(), expected));

    EXPECT_EQ(nw::toolset::snapshot_item_property_records(
                  runtime, item->handle()),
        expected);

    const auto* rows = item_rows(
        runtime, item->handle(), "get_applied_item_property_rows");
    ASSERT_NE(rows, nullptr);
    ASSERT_EQ(rows->size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index) {
        nw::smalls::Value row;
        ASSERT_TRUE(rows->get_value(index, row, runtime));
        EXPECT_EQ(read_int_field(runtime, row, "index"), index);
        EXPECT_EQ(read_int_field(runtime, row, "prop_type"),
            expected[index].prop_type);
        EXPECT_EQ(read_int_field(runtime, row, "subtype"),
            expected[index].subtype);
        EXPECT_EQ(read_int_field(runtime, row, "param_table"),
            expected[index].param_table);
        EXPECT_EQ(read_int_field(runtime, row, "param_value"),
            expected[index].param_value);
        EXPECT_EQ(read_int_field(runtime, row, "cost_table"),
            expected[index].cost_table);
        EXPECT_EQ(read_int_field(runtime, row, "cost_value"),
            expected[index].cost_value);
    }
}
