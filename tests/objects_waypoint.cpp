#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

namespace fs = std::filesystem;

TEST(Waypoint, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load<nw::Waypoint>(fs::path("test_data/user/development/wp_behexit001.utw"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Waypoint::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Waypoint>();
    EXPECT_TRUE(ent2);
    EXPECT_TRUE(nw::Waypoint::deserialize(ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::Waypoint::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);

    std::ofstream f{"tmp/wp_behexit001.utw.json"};
    f << std::setw(4) << j;
}

TEST(Waypoint, GffDeserialize)
{
    auto obj = nw::kernel::objects().load<nw::Waypoint>(fs::path("test_data/user/development/wp_behexit001.utw"));
    EXPECT_TRUE(obj);
}

TEST(Waypoint, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/wp_behexit001.utw");
    EXPECT_TRUE(g.valid());

    auto ent = nw::kernel::objects().make<nw::Waypoint>();
    EXPECT_TRUE(ent);

    EXPECT_TRUE(deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/wp_behexit001.utw");

    nw::Gff g2{"tmp/wp_behexit001.utw"};
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

    EXPECT_EQ(g2.head_->struct_offset, g.head_->struct_offset);
    EXPECT_EQ(g2.head_->struct_count, g.head_->struct_count);
    EXPECT_EQ(g2.head_->field_offset, g.head_->field_offset);
    EXPECT_EQ(g2.head_->field_count, g.head_->field_count);
    EXPECT_EQ(g2.head_->label_offset, g.head_->label_offset);
    EXPECT_EQ(g2.head_->label_count, g.head_->label_count);
    EXPECT_EQ(g2.head_->field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(g2.head_->field_data_count, g.head_->field_data_count);
    EXPECT_EQ(g2.head_->field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(g2.head_->field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(g2.head_->list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(g2.head_->list_idx_count, g.head_->list_idx_count);
}
