#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Store.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Store, JsonSerialize)
{
    auto ent = nw::kernel::objects().load<nw::Store>(fs::path("test_data/user/development/storethief002.utm"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Store::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/storethief002.utm.json"};
    f << std::setw(4) << j;
}

TEST(Store, JsonDeserialize)
{
    auto ent = nw::kernel::objects().make<nw::Store>();
    EXPECT_TRUE(ent);

    std::ifstream f{"tmp/storethief002.utm.json"};
    auto j = nlohmann::json::parse(f);
    nw::Store::deserialize(ent, j, nw::SerializationProfile::blueprint);

    EXPECT_EQ(ent->common.resref, "storethief002");
    EXPECT_TRUE(ent->blackmarket);
    EXPECT_EQ(ent->blackmarket_markdown, 25);
    EXPECT_GT(ent->inventory.weapons.items.size(), 0);
    EXPECT_EQ(std::get<nw::Resref>(ent->inventory.weapons.items[0].item), "nw_wswdg001");
}

TEST(Store, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load<nw::Store>(fs::path("test_data/user/development/storethief002.utm"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Store::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Store>();
    nw::Store::deserialize(ent2, j, nw::SerializationProfile::blueprint);
    EXPECT_TRUE(ent2);

    nlohmann::json j2;
    nw::Store::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);
}

#ifdef ROLLNW_ENABLE_LEGACY

TEST(Store, GffDeserialize)
{
    auto ent = nw::kernel::objects().load<nw::Store>(fs::path("test_data/user/development/storethief002.utm"));
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->common.resref, "storethief002");
    EXPECT_TRUE(ent->blackmarket);
    EXPECT_EQ(ent->blackmarket_markdown, 25);
    EXPECT_TRUE(ent->inventory.weapons.items.size() > 0);
    EXPECT_EQ(std::get<nw::Item*>(ent->inventory.weapons.items[0].item)->common.resref, "nw_wswdg001");
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

#endif // ROLLNW_ENABLE_LEGACY
