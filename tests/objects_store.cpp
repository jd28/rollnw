#include <gtest/gtest.h>

#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Store.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Store, JsonSerialize)
{
    auto name = nw::Store::get_name_from_file(fs::path("test_data/user/development/storethief002.utm"));
    EXPECT_EQ(name, "Blackmarket Store");

    auto ent = nw::kernel::objects().load_file<nw::Store>("test_data/user/development/storethief002.utm");
    EXPECT_TRUE(ent);
    EXPECT_TRUE(ent->save("tmp/storethief002.utm.json"));
    name = nw::Store::get_name_from_file(fs::path("tmp/storethief002.utm.json"));
    EXPECT_EQ(name, "Blackmarket Store");
}

TEST(Store, JsonDeserialize)
{
    auto ent = nw::kernel::objects().make<nw::Store>();
    EXPECT_TRUE(ent);

    std::ifstream f{"tmp/storethief002.utm.json"};
    auto j = nlohmann::json::parse(f);
    nw::deserialize(ent, j, nw::SerializationProfile::blueprint);

    EXPECT_EQ(ent->common.resref, "storethief002");
    EXPECT_TRUE(ent->blackmarket);
    EXPECT_EQ(ent->blackmarket_markdown, 25);
    EXPECT_GT(ent->inventory.weapons.size(), 0);
    EXPECT_EQ(ent->inventory.weapons.items[0].item.as<nw::Resref>(), "nw_wswdg001");
}

TEST(Store, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load_file<nw::Store>("test_data/user/development/storethief002.utm");
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Store>();
    nw::deserialize(ent2, j, nw::SerializationProfile::blueprint);
    EXPECT_TRUE(ent2);

    nlohmann::json j2;
    nw::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);
}

TEST(Store, GffDeserialize)
{
    auto ent = nw::kernel::objects().load_file<nw::Store>("test_data/user/development/storethief002.utm");
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->common.resref, "storethief002");
    EXPECT_TRUE(ent->blackmarket);
    EXPECT_EQ(ent->blackmarket_markdown, 25);
    EXPECT_TRUE(ent->inventory.weapons.size() > 0);
    EXPECT_EQ(ent->inventory.weapons.items[0].item.as<nw::Item*>()->common.resref, "nw_wswdg001");
}

TEST(Store, GffRoundTrip)
{
    nw::Gff g("test_data/user/development/storethief002.utm");
    EXPECT_TRUE(g.valid());

    auto ent = nw::kernel::objects().make<nw::Store>();
    EXPECT_TRUE(ent);
    deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint);

    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/storethief002_2.utm");

    nw::Gff g2("tmp/storethief002_2.utm");
    EXPECT_TRUE(g2.valid());

    // Problem: store pages aren't saved in the same order as toolset
    // EXPEEQRUE(nw::gff_to_gffjson(g) , nw::gff_to_gffjson(g2));

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

TEST(Store, Inventory)
{
    auto ent = nw::kernel::objects().load_file<nw::Store>("test_data/user/development/storethief002.utm");
    EXPECT_TRUE(ent);
    EXPECT_EQ(ent->inventory.weapons.pages(), 1);
    EXPECT_TRUE(ent->inventory.weapons.add_page());
    EXPECT_EQ(ent->inventory.weapons.pages(), 2);
}
