#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/string.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

bool is_null_model_name(std::string_view name)
{
    return name.empty()
        || nw::string::icmp(name, "null")
        || nw::string::icmp(name, "none")
        || nw::string::icmp(name, "****");
}

std::optional<std::string> door_table_model_name(const nw::StaticTwoDA& table, size_t row, nw::StringView column)
{
    std::string model_name;
    if (!table.get_to(row, column, model_name, false) || is_null_model_name(model_name)) {
        return {};
    }
    return model_name;
}

bool model_resource_exists(std::string_view model_name)
{
    return nw::kernel::resman().contains({nw::Resref{model_name}, nw::ResourceType::mdl});
}

std::optional<size_t> find_door_table_model_row(const nw::StaticTwoDA& table, nw::StringView column)
{
    for (size_t row = 1; row < table.rows(); ++row) {
        auto model_name = door_table_model_name(table, row, column);
        if (model_name && model_resource_exists(*model_name)) {
            return row;
        }
    }
    return {};
}

void expect_same_resref(const nw::Resref& actual, std::string_view expected)
{
    EXPECT_TRUE(nw::string::icmp(actual.view(), expected))
        << "expected " << expected << ", got " << actual.view();
}

struct DoorLookup {
    bool resolved = false;
    nw::Resref model;
    std::string error;
    std::string table;
    std::string column;
    std::string selector;
    int32_t row = -1;
    bool generic = false;

    bool valid() const noexcept { return resolved && !model.empty(); }
};

nw::smalls::Value script_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    const nw::smalls::StructDef* def = rt.get_struct_def(value.type_id);
    if (!def) {
        return {};
    }

    uint32_t index = def->field_index(field);
    if (index == UINT32_MAX) {
        return {};
    }

    const auto& fd = def->fields[index];
    return rt.read_value_field_at_offset(value, fd.offset, fd.type_id);
}

int32_t script_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value,
    const char* field, int32_t fallback = 0)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.int_type() ? field_value.data.ival : fallback;
}

bool script_bool_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    return field_value.type_id == rt.bool_type() && field_value.data.bval;
}

std::string script_string_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    if (field_value.type_id != rt.string_type()) {
        return {};
    }
    return std::string{nw::smalls::ScriptString{field_value.data.hptr}.view(rt)};
}

nw::Resref script_resref_field(nw::smalls::Runtime& rt, const nw::smalls::Value& value, const char* field)
{
    nw::smalls::Value field_value = script_field(rt, value, field);
    nw::smalls::TypeID resref_type = rt.type_id("core.types.ResRef", false);
    if (field_value.type_id != resref_type) {
        return {};
    }

    const auto* resref = static_cast<const nw::Resref*>(rt.get_value_data_ptr(field_value));
    return resref ? *resref : nw::Resref{};
}

DoorLookup resolve_door_model_by_state(int32_t appearance, int32_t generic_type)
{
    auto& rt = nw::kernel::runtime();
    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(appearance));
    args.push_back(nw::smalls::Value::make_int(generic_type));

    auto executed = rt.execute_script("nwn1.doors", "resolve_door_model_by_state", args);
    DoorLookup result;
    if (!executed.ok()) {
        result.error = executed.error_message;
        return result;
    }

    result.model = script_resref_field(rt, executed.value, "model");
    result.error = script_string_field(rt, executed.value, "error");
    result.table = script_string_field(rt, executed.value, "table");
    result.column = script_string_field(rt, executed.value, "column");
    result.selector = script_string_field(rt, executed.value, "selector");
    result.row = script_int_field(rt, executed.value, "row", -1);
    result.generic = script_bool_field(rt, executed.value, "generic");
    result.resolved = script_bool_field(rt, executed.value, "valid");
    return result;
}

int32_t door_state_int(nw::Door* door, const char* field)
{
    auto& rt = nw::kernel::runtime();
    auto tid = rt.type_id("core.door.DoorState", false);
    if (!door || tid == nw::smalls::invalid_type_id) { return 0; }

    auto ref = rt.find_propset_ref(tid, door->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return 0; }

    const auto* def = rt.get_struct_def(tid);
    if (!def) { return 0; }

    uint32_t idx = def->field_index(field);
    if (idx == UINT32_MAX) { return 0; }

    auto value = rt.read_value_field_at_offset(ref, def->fields[idx].offset, rt.int_type());
    return value.type_id == rt.int_type() ? value.data.ival : 0;
}

} // namespace

TEST(Door, JsonSerialize)
{
    auto door = nw::kernel::objects().load_file<nw::Door>("test_data/user/development/door_ttr_002.utd");
    auto name = nw::Door::get_name_from_file(fs::path("test_data/user/development/door_ttr_002.utd"));
    EXPECT_EQ(name, "Door");

    nlohmann::json j;
    EXPECT_TRUE(nw::serialize(door, j, nw::SerializationProfile::blueprint));

    auto* door2 = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(door2, nullptr);
    EXPECT_TRUE(nw::deserialize(door2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    EXPECT_TRUE(nw::serialize(door2, j2, nw::SerializationProfile::blueprint));
    EXPECT_EQ(j, j2);

    std::ofstream f{"tmp/door_ttr_002.utd.json"};
    f << std::setw(4) << j;
    f.close();

    name = nw::Door::get_name_from_file(fs::path("tmp/door_ttr_002.utd.json"));
    EXPECT_EQ(name, "Door");
}

TEST(Door, GffDeserialize)
{
    auto door = nw::kernel::objects().load_file<nw::Door>("test_data/user/development/door_ttr_002.utd");

    EXPECT_EQ(door->resref, "door_ttr_002");
    EXPECT_EQ(door_state_int(door, "appearance"), 0);
    EXPECT_EQ(door_state_int(door, "plot"), 0);
    EXPECT_EQ(door_state_int(door, "locked"), 0);
}

TEST(Door, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/door_ttr_002.utd");
    EXPECT_TRUE(g.valid());

    auto door = nw::kernel::objects().load_file<nw::Door>("test_data/user/development/door_ttr_002.utd");

    nw::GffBuilder oa = serialize(door, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/door_ttr_002.utd");

    nw::Gff g2("tmp/door_ttr_002.utd");
    EXPECT_TRUE(g2.valid());
    EXPECT_EQ(nw::gff_to_gffjson(g), nw::gff_to_gffjson(g2));

    EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);
}

TEST(Door, ResolveGenericModelUsesGenericDoors)
{
    auto* genericdoors = nw::kernel::twodas().get("genericdoors");
    ASSERT_NE(genericdoors, nullptr);

    auto row = find_door_table_model_row(*genericdoors, "ModelName");
    if (!row) {
        GTEST_SKIP() << "genericdoors.2da has no model suitable for this test";
    }
    auto model_name = door_table_model_name(*genericdoors, *row, "ModelName");
    ASSERT_TRUE(model_name);

    auto lookup = resolve_door_model_by_state(0, static_cast<int32_t>(*row));
    ASSERT_TRUE(lookup.valid()) << lookup.error;
    EXPECT_TRUE(lookup.generic);
    EXPECT_EQ(lookup.row, static_cast<int32_t>(*row));
    EXPECT_TRUE(nw::string::icmp(lookup.table, "genericdoors"));
    EXPECT_TRUE(nw::string::icmp(lookup.column, "ModelName"));
    EXPECT_TRUE(nw::string::icmp(lookup.selector, "generic type"));
    expect_same_resref(lookup.model, *model_name);
}

TEST(Door, ResolveExplicitModelUsesDoorTypes)
{
    auto* doortypes = nw::kernel::twodas().get("doortypes");
    ASSERT_NE(doortypes, nullptr);

    auto row = find_door_table_model_row(*doortypes, "Model");
    if (!row) {
        GTEST_SKIP() << "doortypes.2da has no model suitable for this test";
    }
    auto model_name = door_table_model_name(*doortypes, *row, "Model");
    ASSERT_TRUE(model_name);

    auto lookup = resolve_door_model_by_state(static_cast<int32_t>(*row), 0);
    ASSERT_TRUE(lookup.valid()) << lookup.error;
    EXPECT_FALSE(lookup.generic);
    EXPECT_EQ(lookup.row, static_cast<int32_t>(*row));
    EXPECT_TRUE(nw::string::icmp(lookup.table, "doortypes"));
    EXPECT_TRUE(nw::string::icmp(lookup.column, "Model"));
    EXPECT_TRUE(nw::string::icmp(lookup.selector, "appearance"));
    expect_same_resref(lookup.model, *model_name);
}
