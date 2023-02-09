#ifdef ROLLNW_ENABLE_LEGACY

#include <gtest/gtest.h>

#include <nw/legacy/Palette.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST(Palette, LoadCreature)
{
    nw::Gff g{"test_data/user/scratch/creaturepalstd.itp"};
    nw::Palette c{g};
    EXPECT_TRUE(c.root.children.size() > 0);
    auto node = c.root.children[0];

    EXPECT_EQ(node.type, nw::PaletteNodeType::branch);
    EXPECT_TRUE(node.strref != std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(node.children.size() > 0);

    auto catnode = node.children[0];
    EXPECT_EQ(catnode.type, nw::PaletteNodeType::category);
    EXPECT_TRUE(catnode.children.size() > 0);

    auto bluenode = catnode.children[0];
    EXPECT_EQ(bluenode.type, nw::PaletteNodeType::blueprint);
    EXPECT_EQ(bluenode.resref, "nw_battdevour");
    EXPECT_TRUE(bluenode.cr >= 11.0f);
    EXPECT_EQ(bluenode.faction, "Hostile");

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
    EXPECT_TRUE(c.root.children.size() > 0);
}

TEST(Palette, JsonConversion)
{
    nw::Gff g{"test_data/user/scratch/tde01palstd.itp"};
    nw::Palette c{g};
    EXPECT_TRUE(c.valid());
    auto j = c.to_json(nw::ResourceType::set);
    std::ofstream f{"tmp/tde01palstd.itp.json"};
    f << std::setw(4) << j;

    nw::Gff g2{"test_data/user/scratch/creaturepalstd.itp"};
    nw::Palette c2{g2};
    auto j2 = c2.to_json(nw::ResourceType::utc);
    std::ofstream f2{"tmp/creaturepalstd.itp.json"};
    f2 << std::setw(4) << j2;
}

#endif // ROLLNW_ENABLE_LEGACY
