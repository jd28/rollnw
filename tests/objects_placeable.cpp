#include <gtest/gtest.h>

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Placeable, GffDeserialize)
{
    auto ent = nw::kernel::objects().load_file<nw::Placeable>("test_data/user/development/arrowcorpse001.utp");
    EXPECT_TRUE(ent);

    auto name = nw::Placeable::get_name_from_file(fs::path("test_data/user/development/arrowcorpse001.utp"));
    EXPECT_EQ(name, "Arrow-filled corpse");

    EXPECT_EQ(ent->common.resref, "arrowcorpse001");
    EXPECT_EQ(ent->appearance, 109u);
    EXPECT_TRUE(!ent->plot);
    EXPECT_TRUE(ent->static_);
    EXPECT_TRUE(!ent->useable);
}

TEST(PlaceableInfo, ParsesLightFields)
{
    nw::StaticTwoDA placeables{fs::path("test_data/user/development/placeables.2da")};
    ASSERT_TRUE(placeables.is_valid());

    nw::PlaceableInfo brazier{placeables.row(57)};
    EXPECT_TRUE(brazier.has_light());
    EXPECT_EQ(brazier.light_color, 6);
    EXPECT_FLOAT_EQ(brazier.light_offset_x, 0.003989f);
    EXPECT_FLOAT_EQ(brazier.light_offset_y, 0.010825f);
    EXPECT_FLOAT_EQ(brazier.light_offset_z, 0.874821f);

    nw::PlaceableInfo armoire{placeables.row(0)};
    EXPECT_FALSE(armoire.has_light());
}

TEST(Placeable, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load_file<nw::Placeable>("test_data/user/development/arrowcorpse001.utp");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);

    nw::Placeable ent2;
    EXPECT_TRUE(nw::deserialize(&ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::serialize(&ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);

    EXPECT_TRUE(ent->save("tmp/arrowcorpse001.utp.json"));
    auto name = nw::Placeable::get_name_from_file(fs::path("tmp/arrowcorpse001.utp.json"));
    EXPECT_EQ(name, "Arrow-filled corpse");
}

TEST(Placeable, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/arrowcorpse001.utp");
    EXPECT_TRUE(g.valid());

    nw::Placeable ent;
    EXPECT_TRUE(deserialize(&ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = serialize(&ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/arrowcorpse001.utp");

    nw::Gff g2{"tmp/arrowcorpse001.utp"};
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
