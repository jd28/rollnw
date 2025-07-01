#include <gtest/gtest.h>

#include <nw/objects/Encounter.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Encounter, GffDeserialize)
{
    auto name = nw::Encounter::get_name_from_file(fs::path("test_data/user/development/boundelementallo.ute"));
    EXPECT_EQ(name, "Bound Elemental Lord");

    auto enc = nw::kernel::objects().load_file<nw::Encounter>("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(enc);

    EXPECT_EQ(enc->common.resref, "boundelementallo");
    EXPECT_TRUE(!!enc->player_only);
    EXPECT_EQ(enc->scripts.on_entered, "");
    EXPECT_TRUE(enc->creatures.size() > 0);
    EXPECT_EQ(enc->creatures[0].resref, "tyn_bound_acid_l");
}

TEST(Encounter, JsonSerialize)
{
    auto enc = nw::kernel::objects().load_file<nw::Encounter>("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(enc);

    nlohmann::json j;
    nw::serialize(enc, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/boundelementallo.ute.json"};
    f << std::setw(4) << j;
    f.close();

    auto name = nw::Encounter::get_name_from_file(fs::path("tmp/boundelementallo.ute.json"));
    EXPECT_EQ(name, "Bound Elemental Lord");
}

TEST(Encounter, JsonRoundTrip)
{
    auto enc = nw::kernel::objects().load_file<nw::Encounter>("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(enc);

    nlohmann::json j;
    nw::serialize(enc, j, nw::SerializationProfile::blueprint);

    nw::Encounter enc2;
    EXPECT_TRUE(nw::deserialize(&enc2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::serialize(enc, j2, nw::SerializationProfile::blueprint);
    EXPECT_EQ(j, j2);
}

TEST(Encounter, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/boundelementallo.ute");
    EXPECT_TRUE(g.valid());

    nw::Encounter enc;
    EXPECT_TRUE(deserialize(&enc, g.toplevel(),
        nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = serialize(&enc, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/boundelementallo_2.ute");
    nw::Gff g2{"tmp/boundelementallo_2.ute"};

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
