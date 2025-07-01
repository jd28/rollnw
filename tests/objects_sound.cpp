#include <gtest/gtest.h>

#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Sound, JsonRoundTrip)
{
    auto name = nw::Sound::get_name_from_file(fs::path("test_data/user/development/blue_bell.uts"));
    EXPECT_EQ(name, "Blue Bell Rings");

    auto ent = nw::kernel::objects().load_file<nw::Sound>("test_data/user/development/blue_bell.uts");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);

    nw::Sound ent2;
    EXPECT_TRUE(nw::deserialize(&ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::serialize(&ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);

    EXPECT_TRUE(ent->save("tmp/blue_bell.uts.json"));
    name = nw::Sound::get_name_from_file(fs::path("tmp/blue_bell.uts.json"));
    EXPECT_EQ(name, "Blue Bell Rings");
}

TEST(Sound, GffDeserialize)
{
    auto ent = nw::kernel::objects().load_file<nw::Sound>("test_data/user/development/blue_bell.uts");
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->common.resref, "blue_bell");
}

TEST(Sound, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/blue_bell.uts");
    EXPECT_TRUE(g.valid());

    nw::Sound ent;
    deserialize(&ent, g.toplevel(), nw::SerializationProfile::blueprint);

    nw::GffBuilder oa = serialize(&ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/blue_bell.uts");

    nw::Gff g2("tmp/blue_bell.uts");
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
