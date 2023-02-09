#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Trigger, GffDeserialize)
{
    auto ent = nw::kernel::objects().load<nw::Trigger>(fs::path("test_data/user/development/pl_spray_sewage.utt"));
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->common.resref, "pl_spray_sewage");
    EXPECT_EQ(ent->portrait, 0);
    EXPECT_EQ(ent->loadscreen, 0);
    EXPECT_EQ(ent->scripts.on_enter, "pl_trig_sewage");
}

TEST(Trigger, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load<nw::Trigger>(fs::path("test_data/user/development/pl_spray_sewage.utt"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Trigger::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Trigger>();
    EXPECT_TRUE(ent2);
    EXPECT_TRUE(nw::Trigger::deserialize(ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    EXPECT_TRUE(nw::Trigger::serialize(ent2, j2, nw::SerializationProfile::blueprint));

    EXPECT_EQ(j, j2);

    std::ofstream f{"tmp/pl_spray_sewage.utt.json"};
    f << std::setw(4) << j;
}

TEST(Trigger, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/pl_spray_sewage.utt");
    EXPECT_TRUE(g.valid());

    auto ent = nw::kernel::objects().make<nw::Trigger>();
    EXPECT_TRUE(ent);
    EXPECT_TRUE(deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_spray_sewage_2.utt");

    nw::Gff g2{"tmp/pl_spray_sewage_2.utt"};
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
