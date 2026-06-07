#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/util/string.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

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

} // namespace

TEST(Door, JsonSerialize)
{
    auto door = nw::kernel::objects().load_file<nw::Door>("test_data/user/development/door_ttr_002.utd");
    auto name = nw::Door::get_name_from_file(fs::path("test_data/user/development/door_ttr_002.utd"));
    EXPECT_EQ(name, "Door");

    nlohmann::json j;
    EXPECT_TRUE(nw::serialize(door, j, nw::SerializationProfile::blueprint));

    nw::Door door2;
    EXPECT_TRUE(nw::deserialize(&door2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    EXPECT_TRUE(nw::serialize(&door2, j2, nw::SerializationProfile::blueprint));
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

    EXPECT_EQ(door->common.resref, "door_ttr_002");
    EXPECT_EQ(door->appearance, 0u);
    EXPECT_TRUE(!door->plot);
    EXPECT_TRUE(!door->lock.locked);
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

    nw::Door door;
    door.appearance = 0;
    door.generic_type = static_cast<uint32_t>(*row);

    auto lookup = nw::resolve_door_model(door);
    ASSERT_TRUE(lookup.valid()) << lookup.error;
    EXPECT_TRUE(lookup.generic);
    EXPECT_EQ(lookup.row, *row);
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

    nw::Door door;
    door.appearance = static_cast<uint32_t>(*row);
    door.generic_type = 0;

    auto lookup = nw::resolve_door_model(door);
    ASSERT_TRUE(lookup.valid()) << lookup.error;
    EXPECT_FALSE(lookup.generic);
    EXPECT_EQ(lookup.row, *row);
    EXPECT_TRUE(nw::string::icmp(lookup.table, "doortypes"));
    EXPECT_TRUE(nw::string::icmp(lookup.column, "Model"));
    EXPECT_TRUE(nw::string::icmp(lookup.selector, "appearance"));
    expect_same_resref(lookup.model, *model_name);
}
