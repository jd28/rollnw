#include "item_gff_builders.hpp"

#include <gtest/gtest.h>

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using namespace std::literals;

namespace {

bool execute_item_bool_script(
    nw::smalls::Runtime& rt,
    const char* module_name,
    std::string_view source,
    const nw::Vector<nw::smalls::Value>& args)
{
    auto* script = rt.load_module_from_source(module_name, source);
    if (!script) {
        ADD_FAILURE() << "failed to load script " << module_name;
        return false;
    }
    if (script->errors() != 0) {
        ADD_FAILURE() << "script has errors: " << module_name;
        return false;
    }

    auto result = rt.execute_script(script, "main", args);
    if (!result.ok()) {
        ADD_FAILURE() << result.error_message;
        return false;
    }
    if (result.value.type_id != rt.bool_type()) {
        ADD_FAILURE() << "script did not return bool: " << module_name;
        return false;
    }
    return result.value.data.bval;
}

bool execute_item_bool_script(nw::smalls::Runtime& rt, const char* module_name, std::string_view source)
{
    nw::Vector<nw::smalls::Value> args;
    return execute_item_bool_script(rt, module_name, source, args);
}

bool update_item_standalone_visual(nw::Item* item, const char* module_name)
{
    if (!item) { return false; }

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import nwn1.item as I;

        fn main(item: Item): bool {
            return I.update_standalone_visual(item, true);
        }
    )";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item->handle()));
    return execute_item_bool_script(rt, module_name, source, args);
}

bool item_has_propset_properties(nw::Item* item, const char* module_name)
{
    if (!item) { return false; }

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.array as Array;
        import core.item as I;

        fn main(item: Item): bool {
            var stats = get_propset!(I.ItemStats)(item);
            return Array.len(stats.item_properties) > 0;
        }
    )";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item->handle()));
    return execute_item_bool_script(rt, module_name, source, args);
}

bool script_has_item_property(nw::Item* item, nw::ItemPropertyType type, int32_t subtype, const char* module_name)
{
    if (!item) { return false; }

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.item as I;

        fn main(item: Item, prop_type: int, subtype: int): bool {
            return I.has_property(item, prop_type, subtype);
        }
    )";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item->handle()));
    args.push_back(nw::smalls::Value::make_int(*type));
    args.push_back(nw::smalls::Value::make_int(subtype));
    return execute_item_bool_script(rt, module_name, source, args);
}

int execute_item_int_script(
    nw::smalls::Runtime& rt,
    const char* module_name,
    std::string_view source,
    const nw::Vector<nw::smalls::Value>& args)
{
    auto* script = rt.load_module_from_source(module_name, source);
    if (!script) {
        ADD_FAILURE() << "failed to load script " << module_name;
        return -1;
    }
    if (script->errors() != 0) {
        ADD_FAILURE() << "script has errors: " << module_name;
        return -1;
    }

    auto result = rt.execute_script(script, "main", args);
    if (!result.ok()) {
        ADD_FAILURE() << result.error_message;
        return -1;
    }
    return result.value.type_id == rt.int_type() ? result.value.data.ival : -1;
}

int item_model_type(nw::Item* item, const char* module_name)
{
    if (!item) { return -1; }

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import nwn1.item as I;

        fn main(item: Item): int {
            return I.get_model_type(item);
        }
    )";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item->handle()));
    return execute_item_int_script(rt, module_name, source, args);
}

int item_value_from_script(const nw::Item* item)
{
    if (!item) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(item->handle()));
    return nwn1::bridge::call_nwn1_module_int("nwn1.item", "calculate_value", args).value_or(0);
}

int item_ac_from_script(const nw::Item* item)
{
    if (!item) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(item->handle()));
    return nwn1::bridge::call_nwn1_module_int("nwn1.item", "calculate_item_ac", args).value_or(0);
}

std::pair<int, int> item_inventory_dimensions(nw::Item* item, const char* module_name)
{
    if (!item) { return {-1, -1}; }

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.array as A;
        import core.item as I;
        import nwn1.rules as R;

        fn main(item: Item): int {
            var stats = get_propset!(I.ItemStats)(item);
            var baseitems = load_config!(R.BaseItemEntry)("nwn1.data.baseitems");
            if (stats.base_item < 0 || stats.base_item >= A.len(baseitems)) {
                return -1;
            }
            var baseitem = A.get(baseitems, stats.base_item);
            if (baseitem.id != stats.base_item) {
                return -1;
            }
            return baseitem.inventory_width * 100 + baseitem.inventory_height;
        }
    )";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item->handle()));
    const int packed = execute_item_int_script(rt, module_name, source, args);
    if (packed < 0) { return {-1, -1}; }
    return {packed / 100, packed % 100};
}

void expect_native_item_layout(nw::Item* item, const char* module_name)
{
    ASSERT_NE(item, nullptr);

    const auto* layout = nw::kernel::objects().components().find_item_layout(item->handle());
    ASSERT_NE(layout, nullptr);

    const auto [width, height] = item_inventory_dimensions(item, module_name);
    ASSERT_GT(width, 0);
    ASSERT_GT(height, 0);
    EXPECT_EQ(layout->inventory_width, width);
    EXPECT_EQ(layout->inventory_height, height);
}

bool invalidate_base_item_and_sync_layout(nw::Item* item)
{
    if (!item) { return false; }

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.item as Base;
        import nwn1.item as I;

        fn main(item: Item): bool {
            var stats = get_propset!(Base.ItemStats)(item);
            stats.base_item = -1;
            return !I.sync_native_layout(item);
        }
    )";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, item->handle()));
    return execute_item_bool_script(rt, "test.item_invalid_baseitem_clears_native_layout", source, args);
}

const nw::ObjectVisualModel* find_item_visual_row(const nw::Item* item, nw::ItemModelParts::type part)
{
    if (!item) { return nullptr; }

    const auto* visual = nw::kernel::objects().components().find_visual(item->handle());
    if (!visual) { return nullptr; }

    for (auto it = visual->models.rbegin(); it != visual->models.rend(); ++it) {
        if (it->kind == nw::ObjectVisualModelKind::item_model
            && it->part == static_cast<int32_t>(part)) {
            return &*it;
        }
    }
    return nullptr;
}

void expect_plt_color(const nw::ObjectVisualModel& row, size_t layer, uint8_t value)
{
    const auto layer_mask = 1u << layer;
    EXPECT_NE(row.plt_color_mask & layer_mask, 0u);
    EXPECT_EQ(row.plt_colors.data[layer], value);
}

} // namespace

TEST(Item, Colors)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto heavy1 = nw::kernel::objects().load<nw::Item>("x2_mdrowar030"sv);
    EXPECT_TRUE(heavy1);
    const auto* row = find_item_visual_row(heavy1, nw::ItemModelParts::model1);
    ASSERT_NE(row, nullptr);
    expect_plt_color(*row, nw::plt_layer_metal1, 3);
    expect_plt_color(*row, nw::plt_layer_metal2, 25);
}

TEST(Item, GffDeserializeCompositeColors)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    nw::GffBuilder out{nw::Item::serial_id};
    out.top.add_field("TemplateResRef", nw::Resref{"testbow"})
        .add_field("LocalizedName", nw::LocString{})
        .add_field("Tag", nw::String{"testbow"})
        .add_field("Comment", nw::String{})
        .add_field("PaletteID", uint8_t{0})
        .add_field("Description", nw::LocString{})
        .add_field("DescIdentified", nw::LocString{})
        .add_field("Cost", uint32_t{0})
        .add_field("AddCost", uint32_t{0})
        .add_field("BaseItem", int32_t{8})
        .add_field("StackSize", uint16_t{1})
        .add_field("Charges", uint8_t{0})
        .add_field("Identified", uint8_t{1})
        .add_field("Plot", uint8_t{0})
        .add_field("Stolen", uint8_t{0})
        .add_field("ModelPart1", uint8_t{1})
        .add_field("ModelPart2", uint8_t{2})
        .add_field("ModelPart3", uint8_t{3})
        .add_field("Cloth1Color", uint8_t{11})
        .add_field("Cloth2Color", uint8_t{12})
        .add_field("Leather1Color", uint8_t{13})
        .add_field("Leather2Color", uint8_t{14})
        .add_field("Metal1Color", uint8_t{15})
        .add_field("Metal2Color", uint8_t{16});
    out.top.add_list("PropertiesList");
    out.build();
    ASSERT_TRUE(out.write_to("tmp/composite_colors.uti"));

    nw::Gff gff{"tmp/composite_colors.uti"};
    ASSERT_TRUE(gff.valid());

    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(nw::deserialize(item, gff.toplevel(), nw::SerializationProfile::blueprint));
    const auto* row = find_item_visual_row(item, nw::ItemModelParts::model1);
    ASSERT_NE(row, nullptr);
    expect_plt_color(*row, nw::plt_layer_cloth1, 11);
    expect_plt_color(*row, nw::plt_layer_cloth2, 12);
    expect_plt_color(*row, nw::plt_layer_leather1, 13);
    expect_plt_color(*row, nw::plt_layer_leather2, 14);
    expect_plt_color(*row, nw::plt_layer_metal1, 15);
    expect_plt_color(*row, nw::plt_layer_metal2, 16);
    nw::kernel::objects().destroy(item->handle());
}

TEST(Item, ResolveCompositeVisual)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.types as T;
        import core.visual as V;
        import nwn1.constants as Const;
        import nwn1.item as I;

        fn main(): bool {
            var visual = I.resolve_visual_by_state(0, 1, 2, 3);
            if (!visual.baseitem_valid || visual.requires_wearer_context || visual.attachment_count != 3) {
                return false;
            }
            if (T.resref_to_string(visual.default_model) != "it_bag") {
                return false;
            }

            var a0 = I.resolve_visual_row_by_state(0, 1, 2, 3, 0, Const.equip_index_righthand, T.resref("rhand"));
            if (a0.kind != V.visual_model_kind_item_model
                || a0.slot != Const.equip_index_righthand as int
                || T.resref_to_string(a0.attach_to) != "rhand"
                || T.resref_to_string(a0.model) != "wswss_b_001"
                || a0.part != I.item_model_part_model1) {
                return false;
            }

            var a1 = I.resolve_visual_row_by_state(0, 1, 2, 3, 1, Const.equip_index_righthand, T.resref("rhand"));
            if (T.resref_to_string(a1.model) != "wswss_m_002" || a1.part != I.item_model_part_model2) {
                return false;
            }

            var a2 = I.resolve_visual_row_by_state(0, 1, 2, 3, 2, Const.equip_index_righthand, T.resref("rhand"));
            if (T.resref_to_string(a2.model) != "wswss_t_003" || a2.part != I.item_model_part_model3) {
                return false;
            }

            var icon = I.resolve_icon_by_state(0, I.item_model_part_model2, 2, false);
            return icon.baseitem_valid
                && icon.allow_default
                && !icon.is_plt
                && T.resref_to_string(icon.resref) == "iwswss_m_002"
                && T.resref_to_string(icon.default_icon) == "iwswss";
        }
    )";
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_resolve_composite_visual", source));
}

TEST(Item, ResolveVisualUsesPropsetBaseItemAndSmallsConfig)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("test_data/user/development/nwn1"));

    rollnw::tests::TestItemGff item_spec{
        .base_item = nw::BaseItem::make(0),
        .model_shape = rollnw::tests::TestItemModelShape::composite,
        .model_parts = {1, 2, 3},
    };
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);

    std::string_view source = R"(
        import core.item as I;

        fn main(item: Item, base_item: int): int {
            var stats = get_propset!(I.ItemStats)(item);
            stats.base_item = base_item;
            return stats.base_item;
        }
    )";

    auto* script = rt.load_module_from_source("test.item_propset_baseitem_override", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);
    args.push_back(nw::smalls::Value::make_int(112));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.value.data.ival, 112);

    auto stats_tid = rt.type_id("core.item.ItemStats", false);
    ASSERT_NE(stats_tid, nw::smalls::invalid_type_id);
    auto stats_ref = rt.find_propset_ref(stats_tid, item->handle());
    ASSERT_NE(stats_ref.type_id, nw::smalls::invalid_type_id);
    auto* stats_type = rt.get_type(stats_tid);
    ASSERT_NE(stats_type, nullptr);
    auto* stats_def = rt.type_table_.get(stats_type->type_params[0].as<nw::smalls::StructID>());
    ASSERT_NE(stats_def, nullptr);
    uint32_t base_item_field = stats_def->field_index("base_item");
    ASSERT_NE(base_item_field, UINT32_MAX);
    auto base_item_value = rt.read_value_field_at_offset(
        stats_ref,
        stats_def->fields[base_item_field].offset,
        stats_def->fields[base_item_field].type_id);
    ASSERT_EQ(base_item_value.data.ival, 112);

    auto baseitem_tid = rt.type_id("nwn1.rules.BaseItemEntry", false);
    ASSERT_NE(baseitem_tid, nw::smalls::invalid_type_id);
    auto config_array_value = rt.load_config_array_value("nwn1.data.baseitems", baseitem_tid);
    ASSERT_NE(config_array_value.type_id, nw::smalls::invalid_type_id);
    auto* config_array = rt.get_array_typed(config_array_value.data.hptr);
    ASSERT_NE(config_array, nullptr);
    const void* config_entry = config_array->element_data(112);
    ASSERT_NE(config_entry, nullptr);
    auto* baseitem_type = rt.get_type(baseitem_tid);
    ASSERT_NE(baseitem_type, nullptr);
    auto* baseitem_def = rt.type_table_.get(baseitem_type->type_params[0].as<nw::smalls::StructID>());
    ASSERT_NE(baseitem_def, nullptr);
    auto read_config_int = [&](const char* field_name) {
        int32_t value = -1;
        uint32_t field_index = baseitem_def->field_index(field_name);
        EXPECT_NE(field_index, UINT32_MAX);
        if (field_index != UINT32_MAX) {
            std::memcpy(&value,
                static_cast<const uint8_t*>(config_entry) + baseitem_def->fields[field_index].offset,
                sizeof(value));
        }
        return value;
    };
    ASSERT_EQ(read_config_int("id"), 112);
    ASSERT_EQ(read_config_int("model_type"), 2);

    std::string_view visual_source = R"(
        import core.types as T;
        import nwn1.item as I;

        fn main(item: Item): bool {
            var visual = I.resolve_visual(item);
            if (!visual.baseitem_valid || visual.requires_wearer_context || visual.attachment_count != 3) {
                return false;
            }
            if (T.resref_to_string(visual.default_model) != "it_bag") {
                return false;
            }

            var a0 = I.resolve_visual_row(item, 0);
            if (a0.slot != I.equip_index_invalid
                || T.resref_to_string(a0.attach_to) != ""
                || T.resref_to_string(a0.model) != "tstbi_b_001") {
                return false;
            }

            var a1 = I.resolve_visual_row(item, 1);
            var a2 = I.resolve_visual_row(item, 2);
            if (T.resref_to_string(a1.model) != "tstbi_m_002"
                || T.resref_to_string(a2.model) != "tstbi_t_003") {
                return false;
            }

            var icon = I.resolve_icon(item, I.item_model_part_model2, false);
            return icon.baseitem_valid
                && T.resref_to_string(icon.resref) == "itstbi_m_002"
                && T.resref_to_string(icon.default_icon) == "it_test";
        }
    )";
    nw::Vector<nw::smalls::Value> visual_args;
    visual_args.push_back(item_value);
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_propset_baseitem_visual", visual_source, visual_args));

    nw::kernel::objects().destroy(item->handle());
}

TEST(Item, ResolveVisualUsesItemVisualsPropset)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    rollnw::tests::TestItemGff item_spec{
        .base_item = nw::BaseItem::make(0),
        .model_shape = rollnw::tests::TestItemModelShape::composite,
        .model_parts = {1, 2, 3},
    };
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        import core.item as I;

        fn main(item: Item): int {
            if (!I.set_visual_model_part(item, 0, 4)) { return 0; }
            if (!I.set_visual_model_part(item, 1, 5)) { return 0; }
            if (!I.set_visual_model_part(item, 2, 6)) { return 0; }
            if (!I.set_visual_model_color(item, 0, 11)) { return 0; }
            if (!I.set_visual_part_color(item, 1, 0, 12)) { return 0; }
            if (I.get_visual_model_part(item, 1) != 5) { return 0; }
            if (I.get_visual_model_color(item, 0) != 11) { return 0; }
            if (I.get_visual_part_color(item, 1, 0) != 12) { return 0; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.item_visuals_propset_override", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.value.data.ival, 1);

    std::string_view visual_source = R"(
        import core.types as T;
        import nwn1.item as I;

        fn main(item: Item): bool {
            var visual = I.resolve_visual(item);
            if (!visual.baseitem_valid || visual.attachment_count != 3) {
                return false;
            }

            var a0 = I.resolve_visual_row(item, 0);
            var a1 = I.resolve_visual_row(item, 1);
            var a2 = I.resolve_visual_row(item, 2);
            if (T.resref_to_string(a0.model) != "wswss_b_004"
                || T.resref_to_string(a1.model) != "wswss_m_005"
                || T.resref_to_string(a2.model) != "wswss_t_006") {
                return false;
            }

            var icon = I.resolve_icon(item, I.item_model_part_model2, false);
            return icon.baseitem_valid
                && T.resref_to_string(icon.resref) == "iwswss_m_005";
        }
    )";
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_visuals_propset_visual", visual_source, args));
    ASSERT_TRUE(update_item_standalone_visual(item, "test.item_visuals_propset_update_visual"));

    const auto* model_row = find_item_visual_row(item, nw::ItemModelParts::model1);
    ASSERT_NE(model_row, nullptr);
    expect_plt_color(*model_row, nw::plt_layer_cloth1, 11);
    const auto* part_row = find_item_visual_row(item, nw::ItemModelParts::model2);
    ASSERT_NE(part_row, nullptr);
    expect_plt_color(*part_row, nw::plt_layer_cloth1, 12);

    nlohmann::json json;
    ASSERT_TRUE(nw::serialize(item, json, nw::SerializationProfile::blueprint));
    ASSERT_TRUE(json.contains("core.item.ItemVisuals"));
    const auto& visuals = json.at("core.item.ItemVisuals");
    EXPECT_EQ(visuals.at("model_parts")[0], 4);
    EXPECT_EQ(visuals.at("model_parts")[1], 5);
    EXPECT_EQ(visuals.at("model_colors")[0], 11);
    ASSERT_GT(visuals.at("part_colors").size(), 6);
    EXPECT_EQ(visuals.at("part_colors")[6], 12);

    nw::GffBuilder builder(nw::Item::serial_id);
    ASSERT_TRUE(nw::serialize(item, builder.top, nw::SerializationProfile::blueprint));
    builder.build();
    nw::ResourceData rd;
    rd.bytes = builder.to_byte_array();
    nw::Gff gff{std::move(rd)};
    ASSERT_TRUE(gff.valid());
    uint16_t model_part2 = 0;
    ASSERT_TRUE(gff.toplevel().get_to("xModelPart2", model_part2, false));
    EXPECT_EQ(model_part2, 5);
    uint8_t cloth1 = 0;
    ASSERT_TRUE(gff.toplevel().get_to("Cloth1Color", cloth1, false));
    EXPECT_EQ(cloth1, 11);

    nw::kernel::objects().destroy(item->handle());
}

TEST(Item, ResolveVisualUsesSmallsModelPolicy)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("test_data/user/development/nwn1"));

    std::string_view source = R"(
        import core.types as T;
        import nwn1.item as I;

        fn main(): bool {
            var visual = I.resolve_visual_by_state(112, 1, 2, 3);
            if (!visual.baseitem_valid || visual.attachment_count != 3) {
                return false;
            }
            var a0 = I.resolve_visual_row_by_state(112, 1, 2, 3, 0, T.EquipIndex(-1), T.resref(""));
            var a1 = I.resolve_visual_row_by_state(112, 1, 2, 3, 1, T.EquipIndex(-1), T.resref(""));
            var a2 = I.resolve_visual_row_by_state(112, 1, 2, 3, 2, T.EquipIndex(-1), T.resref(""));
            return T.resref_to_string(a0.model) == "tstbi_b_001"
                && T.resref_to_string(a1.model) == "tstbi_m_002"
                && T.resref_to_string(a2.model) == "tstbi_t_003";
        }
    )";
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_smalls_model_policy_visual", source));

    std::string_view icon_source = R"(
        import core.types as T;
        import nwn1.item as I;

        fn main(): bool {
            var icon = I.resolve_icon_by_state(112, I.item_model_part_model2, 2, false);
            return icon.baseitem_valid
                && T.resref_to_string(icon.resref) == "itstbi_m_002"
                && T.resref_to_string(icon.default_icon) == "it_test";
        }
    )";
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_smalls_model_policy_icon", icon_source));
}

TEST(Item, ResolveArmorVisualRequiresWearerContext)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.types as T;
        import nwn1.item as I;

        fn main(): bool {
            var visual = I.resolve_visual_by_state(16, 0, 0, 0);
            if (!visual.baseitem_valid || !visual.requires_wearer_context || visual.attachment_count != 0) {
                return false;
            }

            var male_icon = I.resolve_icon_by_state(16, I.item_model_part_armor_torso, 28, false);
            if (!male_icon.baseitem_valid
                || !male_icon.allow_default
                || !male_icon.is_plt
                || T.resref_to_string(male_icon.resref) != "ipm_chest028") {
                return false;
            }

            var female_icon = I.resolve_icon_by_state(16, I.item_model_part_armor_torso, 28, true);
            return female_icon.baseitem_valid
                && female_icon.allow_default
                && female_icon.is_plt
                && T.resref_to_string(female_icon.resref) == "ipf_chest028";
        }
    )";
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_armor_visual_requires_wearer", source));
}

TEST(Item, ResolveCloakVisualUsesSmallsCloakModelPolicy)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.types as T;
        import core.visual as V;
        import nwn1.constants as Const;
        import nwn1.item as I;

        fn main(): bool {
            var cloak = I.resolve_visual_by_state(Const.base_item_cloak as int, 1, 0, 0);
            if (!cloak.baseitem_valid || !cloak.requires_wearer_context || cloak.attachment_count != 1) {
                return false;
            }

            var attachment = I.resolve_visual_row_by_state(
                Const.base_item_cloak as int,
                1,
                0,
                0,
                0,
                Const.equip_index_cloak,
                T.resref(""));
            if (attachment.kind != V.visual_model_kind_creature_model_part
                || attachment.slot != Const.equip_index_cloak as int
                || attachment.part != I.item_model_part_model1
                || attachment.source_part != 1
                || attachment.model_part != 4
                || attachment.flags != V.visual_model_flag_requires_wearer) {
                return false;
            }

            var sword = I.resolve_visual_by_state(0, 1, 0, 0);
            if (!sword.baseitem_valid || sword.attachment_count != 1) {
                return false;
            }

            var sword_attachment = I.resolve_visual_row_by_state(0, 1, 0, 0, 0, T.EquipIndex(-1), T.resref(""));
            return sword_attachment.kind == V.visual_model_kind_item_model;
        }
    )";
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_cloak_visual_model_policy", source));
}

TEST(Item, ResolveCloakVisualUsesItemVisualsPropset)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    rollnw::tests::TestItemGff item_spec{
        .base_item = nwn1::base_item_cloak,
        .model_shape = rollnw::tests::TestItemModelShape::layered,
        .model_parts = {2, 0, 0},
        .model_colors = {1, 0, 0, 0, 0, 0},
    };
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        import core.item as I;

        fn main(item: Item): int {
            if (!I.set_visual_model_part(item, 0, 1)) { return 0; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.item_cloak_visual_propset_override", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.value.data.ival, 1);

    std::string_view visual_source = R"(
        import core.types as T;
        import core.visual as V;
        import nwn1.constants as Const;
        import nwn1.item as I;

        fn main(item: Item): bool {
            var visual = I.resolve_visual(item);
            if (!visual.baseitem_valid || visual.attachment_count != 1) {
                return false;
            }

            var attachment = I.resolve_visual_row_for_slot(item, 0, Const.equip_index_cloak, T.resref(""));
            return attachment.kind == V.visual_model_kind_creature_model_part
                && attachment.source_part == 1
                && attachment.model_part == 4;
        }
    )";
    EXPECT_TRUE(execute_item_bool_script(rt, "test.item_cloak_visual_propset_resolve", visual_source, args));

    nw::kernel::objects().destroy(item->handle());
}

TEST(Item, GffDeserializeArmor)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto name = nw::Item::get_name_from_file(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_EQ(name, "Adept's Tunic");

    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(ent);
    auto light = nw::kernel::objects().load<nw::Item>("nw_maarcl004"sv);
    EXPECT_TRUE(light);
    auto medium = nw::kernel::objects().load<nw::Item>("nw_maarcl040"sv);
    EXPECT_TRUE(medium);
    auto heavy1 = nw::kernel::objects().load<nw::Item>("x2_mdrowar030"sv);
    EXPECT_TRUE(heavy1);
    auto heavy2 = nw::kernel::objects().load<nw::Item>("x2_it_adaplate"sv);
    EXPECT_TRUE(heavy2);

    EXPECT_EQ(ent->resref, "cloth028");
    EXPECT_TRUE(item_has_propset_properties(ent, "test.item_armor_has_propset_properties"));
    EXPECT_EQ(item_model_type(ent, "test.item_armor_model_type"),
        static_cast<int>(nw::ItemModelType::armor));
    expect_native_item_layout(ent, "test.item_armor_native_layout");

    EXPECT_EQ(item_ac_from_script(ent), 0);
    EXPECT_EQ(item_ac_from_script(light), 1);
    EXPECT_EQ(item_ac_from_script(medium), 5);
    EXPECT_EQ(item_ac_from_script(heavy1), 7);
    EXPECT_EQ(item_ac_from_script(heavy2), 8);
}

TEST(Item, LocalVariables)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(ent);
    auto* locals = nw::kernel::objects().components().find_locals(ent->handle());
    ASSERT_NE(locals, nullptr);
    EXPECT_GT(locals->size(), 0);
    EXPECT_EQ(locals->get_float("test1"), 1.5f);
    EXPECT_EQ(locals->get_float("doesn't have"), 0.0f);
    EXPECT_EQ(locals->get_string("stringtest"), "0");
    locals->delete_string("stringtest");
    EXPECT_EQ(locals->get_string("stringtest"), "");

    locals->set_string("test", "1");
    EXPECT_EQ(locals->get_string("test"), "1");
    locals->set_int("test", 1);
    EXPECT_EQ(locals->get_int("test"), 1);
    locals->set_float("test", 1.0);
    EXPECT_EQ(locals->get_float("test"), 1.0f);
    locals->clear("test", nw::LocalVarType::float_);
    EXPECT_EQ(locals->get_float("test"), 0.0f);

    locals->set_string("test", "1");
    for (auto type : {nw::LocalVarType::integer,
             nw::LocalVarType::float_,
             nw::LocalVarType::object,
             nw::LocalVarType::location,
             nw::LocalVarType::string}) {
        locals->clear_all(type);
    }
    EXPECT_EQ(locals->get_string("test"), "");
    EXPECT_EQ(locals->size(), 0);
}

TEST(Item, GffDeserializeLayered)
{
    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/wduersc004.uti");
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->resref, "wduersc004");
    EXPECT_TRUE(item_has_propset_properties(ent, "test.item_layered_has_propset_properties"));
    EXPECT_EQ(item_model_type(ent, "test.item_layered_model_type"),
        static_cast<int>(nw::ItemModelType::composite));
    expect_native_item_layout(ent, "test.item_layered_native_layout");
}

TEST(Item, GffDeserializeSimple)
{
    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/pl_aleu_shuriken.uti");
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->resref, "pl_aleu_shuriken");
    EXPECT_TRUE(item_has_propset_properties(ent, "test.item_simple_has_propset_properties"));
    EXPECT_EQ(item_model_type(ent, "test.item_simple_model_type"),
        static_cast<int>(nw::ItemModelType::simple));
    expect_native_item_layout(ent, "test.item_simple_native_layout");
}

TEST(Item, SyncNativeLayoutClearsStaleLayoutOnInvalidBaseItem)
{
    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/pl_aleu_shuriken.uti");
    ASSERT_TRUE(ent);
    ASSERT_NE(nw::kernel::objects().components().find_item_layout(ent->handle()), nullptr);

    EXPECT_TRUE(invalidate_base_item_and_sync_layout(ent));
    EXPECT_EQ(nw::kernel::objects().components().find_item_layout(ent->handle()), nullptr);
}

TEST(Item, SmallsHasPropertyReadsPropset)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    rollnw::tests::TestItemGff item_spec{
        .base_item = nw::BaseItem::make(0),
        .model_shape = rollnw::tests::TestItemModelShape::composite,
        .model_parts = {1, 2, 3},
    };
    item_spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_keen),
        .subtype = 7,
    });
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);

    EXPECT_TRUE(script_has_item_property(item, nwn1::ip_keen, -1, "test.has_item_property_any"));
    EXPECT_TRUE(script_has_item_property(item, nwn1::ip_keen, 7, "test.has_item_property_subtype"));
    EXPECT_FALSE(script_has_item_property(item, nwn1::ip_keen, 8, "test.has_item_property_missing_subtype"));

    nw::kernel::objects().destroy(item->handle());
}

TEST(Item, JsonSerialize)
{
    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/cloth028.uti.json"};
    f << std::setw(4) << j;
    f.close();

    auto name = nw::Item::get_name_from_file(fs::path("tmp/cloth028.uti.json"));
    EXPECT_EQ(name, "Adept's Tunic");
}

TEST(Item, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto* ent2 = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(ent2, nullptr);
    EXPECT_TRUE(nw::deserialize(ent2, j, nw::SerializationProfile::blueprint));
    expect_native_item_layout(ent2, "test.item_json_roundtrip_native_layout");

    nlohmann::json j2;
    nw::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);
    nw::kernel::objects().destroy(ent2->handle());
}

TEST(Item, GffRoundTrip)
{
    auto ent = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(ent);

    nw::Gff g("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(g.valid());

    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/cloth028.uti");

    nw::Gff g2{"tmp/cloth028.uti"};
    EXPECT_TRUE(g2.valid());

    // Problem: local vars arent always saved in same order
    // EXPEEQRUE(nw::gff_to_gffjson(g) , nw::gff_to_gffjson(g2));

    // With updated entries we can no longer round trip.

    // EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    // EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    // EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    // EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    // EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    // EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    // EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    // EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    // EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    // EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    // EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    // EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);
}

TEST(Item, Value)
{
    auto item1 = nw::kernel::objects().load<nw::Item>("x2_it_minoaxe"sv);
    EXPECT_TRUE(item1);
    EXPECT_EQ(item_value_from_script(item1), 233320);

    auto item2 = nw::kernel::objects().load<nw::Item>("nw_maarcl025"sv);
    EXPECT_TRUE(item2);
    EXPECT_EQ(item_value_from_script(item2), 51152);

    auto item3 = nw::kernel::objects().load<nw::Item>("nw_it_novel008"sv);
    EXPECT_TRUE(item3);
    EXPECT_EQ(item_value_from_script(item3), 58575);
}
