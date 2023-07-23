#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/objects/Door.hpp>
#include <nw/serialization/Archives.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Door, JsonSerialize)
{
    auto door = nw::kernel::objects().load<nw::Door>(fs::path("test_data/user/development/door_ttr_002.utd"));

    nlohmann::json j;
    EXPECT_TRUE(nw::Door::serialize(door, j, nw::SerializationProfile::blueprint));

    nw::Door door2;
    EXPECT_TRUE(nw::Door::deserialize(&door2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    EXPECT_TRUE(nw::Door::serialize(&door2, j2, nw::SerializationProfile::blueprint));
    EXPECT_EQ(j, j2);

    std::ofstream f{"tmp/door_ttr_002.utd.json"};
    f << std::setw(4) << j;
}

#ifdef ROLLNW_ENABLE_LEGACY

TEST(Door, GffDeserialize)
{
    auto door = nw::kernel::objects().load<nw::Door>(fs::path("test_data/user/development/door_ttr_002.utd"));

    EXPECT_EQ(door->common.resref, "door_ttr_002");
    EXPECT_EQ(door->appearance, 0u);
    EXPECT_TRUE(!door->plot);
    EXPECT_TRUE(!door->lock.locked);
}

TEST(Door, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/door_ttr_002.utd");
    EXPECT_TRUE(g.valid());

    auto door = nw::kernel::objects().load<nw::Door>(fs::path("test_data/user/development/door_ttr_002.utd"));

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

#endif // ROLLNW_ENABLE_LEGACY
