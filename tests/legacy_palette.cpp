

#include <gtest/gtest.h>

#include <nw/formats/Palette.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

using namespace std::literals;

TEST(Palette, LoadCreature)
{
    nw::Gff g{"test_data/user/scratch/creaturepalstd.itp"};
    nw::Palette c{g};
    EXPECT_TRUE(c.children.size() > 0);
    auto node = c.children[0];

    EXPECT_EQ(node->type, nw::PaletteNodeType::branch);
    EXPECT_TRUE(node->strref != std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(node->children.size() > 0);

    auto catnode = node->children[0];
    EXPECT_EQ(catnode->type, nw::PaletteNodeType::category);
    EXPECT_TRUE(catnode->children.size() > 0);

    auto bluenode = catnode->children[0];
    EXPECT_EQ(bluenode->type, nw::PaletteNodeType::blueprint);
    EXPECT_EQ(bluenode->resref, "nw_battdevour");
    EXPECT_TRUE(bluenode->cr >= 11.0f);
    EXPECT_EQ(bluenode->faction, "Hostile");

    nw::Gff g2{"test_data/user/scratch/creaturepal.itp"};
    nw::Palette p2{g2};
    EXPECT_TRUE(p2.valid());
    EXPECT_EQ(p2.resource_type, nw::ResourceType::utc);
}

TEST(Palette, LoadTileset)
{
    nw::Gff g{"test_data/user/scratch/tde01palstd.itp"};
    nw::Palette c{g};
    EXPECT_TRUE(c.valid());
    EXPECT_TRUE(c.children.size() > 0);
}

TEST(Palette, JsonConversion)
{
    auto data = nw::kernel::resman().demand({"creaturepal"sv, nw::ResourceType::itp});
    nw::Gff g{std::move(data)};
    nw::Palette pal1{g};
    EXPECT_TRUE(pal1.valid());
    auto j1 = pal1.to_json();
    std::ofstream f{"tmp/creaturepal.itp.json"};
    f << std::setw(4) << j1;

    nw::Palette pal2;
    pal2.from_json(j1);
    auto j2 = pal2.to_json();
    EXPECT_EQ(j1, j2);

    nw::Gff g2{"test_data/user/scratch/creaturepalstd.itp"};
    nw::Palette pal3{g2};
    auto j3 = pal3.to_json();
    std::ofstream f2{"tmp/creaturepalstd.itp.json"};
    f2 << std::setw(4) << j3;
}

TEST(Palette, GffRoundTrip)
{
    nw::Gff g{"test_data/user/scratch/creaturepal.itp"};
    EXPECT_TRUE(g.valid());
    nw::Palette f{g};
    nw::GffBuilder out = nw::serialize(f);
    out.write_to("tmp/creaturepal2.itp");

    nw::Gff g2{"tmp/creaturepal2.itp"};
    EXPECT_TRUE(g2.valid());

    // Due to ITPs being a hand edited thing, you can never be certain that these tests will pass,
    // uncomment and confirm via json diff / visually.

    // auto j1 = nw::gff_to_gffjson(g);
    // auto j2 = nw::gff_to_gffjson(g2);

    // EXPECT_EQ(j1, j2);

    // EXPECT_EQ(out.header.struct_offset, g.head_->struct_offset);
    // EXPECT_EQ(out.header.struct_count, g.head_->struct_count);
    // EXPECT_EQ(out.header.field_offset, g.head_->field_offset);
    // EXPECT_EQ(out.header.field_count, g.head_->field_count);
    // EXPECT_EQ(out.header.label_offset, g.head_->label_offset);
    // EXPECT_EQ(out.header.label_count, g.head_->label_count);
    // EXPECT_EQ(out.header.field_data_offset, g.head_->field_data_offset);
    // EXPECT_EQ(out.header.field_data_count, g.head_->field_data_count);
    // EXPECT_EQ(out.header.field_idx_offset, g.head_->field_idx_offset);
    // EXPECT_EQ(out.header.field_idx_count, g.head_->field_idx_count);
    // EXPECT_EQ(out.header.list_idx_offset, g.head_->list_idx_offset);
    // EXPECT_EQ(out.header.list_idx_count, g.head_->list_idx_count);
}
