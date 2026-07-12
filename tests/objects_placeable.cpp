#include <gtest/gtest.h>

#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

int32_t placeable_state_int(nw::Placeable* placeable, const char* field)
{
    auto& rt = nw::kernel::runtime();
    auto tid = rt.type_id("core.placeable.PlaceableState", false);
    if (!placeable || tid == nw::smalls::invalid_type_id) { return 0; }

    auto ref = rt.find_propset_ref(tid, placeable->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return 0; }

    const auto* def = rt.get_struct_def(tid);
    if (!def) { return 0; }

    uint32_t idx = def->field_index(field);
    if (idx == UINT32_MAX) { return 0; }

    auto value = rt.read_value_field_at_offset(ref, def->fields[idx].offset, rt.int_type());
    return value.type_id == rt.int_type() ? value.data.ival : 0;
}

} // namespace

TEST(Placeable, GffDeserialize)
{
    auto ent = nw::kernel::objects().load_file<nw::Placeable>("test_data/user/development/arrowcorpse001.utp");
    EXPECT_TRUE(ent);

    auto name = nw::Placeable::get_name_from_file(fs::path("test_data/user/development/arrowcorpse001.utp"));
    EXPECT_EQ(name, "Arrow-filled corpse");

    EXPECT_EQ(ent->resref, "arrowcorpse001");
    EXPECT_EQ(placeable_state_int(ent, "appearance"), 109);
    ASSERT_TRUE(ent->instantiate());
    const auto* visual = nw::kernel::objects().components().find_visual(ent->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_EQ(visual->appearance, 109);
    ASSERT_EQ(visual->models.size(), 1);
    EXPECT_EQ(visual->models[0].model, nw::Resref{"plc_o01"});
    EXPECT_EQ(visual->models[0].kind, 0);
    EXPECT_EQ(placeable_state_int(ent, "plot"), 0);
    EXPECT_NE(placeable_state_int(ent, "static"), 0);
    EXPECT_EQ(placeable_state_int(ent, "useable"), 0);
}

TEST(Placeable, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load_file<nw::Placeable>("test_data/user/development/arrowcorpse001.utp");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto* ent2 = nw::kernel::objects().make<nw::Placeable>();
    ASSERT_NE(ent2, nullptr);
    EXPECT_TRUE(nw::deserialize(ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);

    EXPECT_TRUE(ent->save("tmp/arrowcorpse001.utp.json"));
    auto name = nw::Placeable::get_name_from_file(fs::path("tmp/arrowcorpse001.utp.json"));
    EXPECT_EQ(name, "Arrow-filled corpse");
}

TEST(Placeable, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/arrowcorpse001.utp");
    EXPECT_TRUE(g.valid());

    auto* ent = nw::kernel::objects().make<nw::Placeable>();
    ASSERT_NE(ent, nullptr);
    EXPECT_TRUE(deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
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
