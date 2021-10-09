#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Palette.hpp>

TEST_CASE("Loading Creature Palette", "[objects]")
{
    nw::Gff g{"test_data/creaturepalstd.itp"};
    nw::Palette c{g.toplevel()};
    REQUIRE(c.root.children.size() > 0);
    auto node = c.root.children[0];

    REQUIRE(node.type == nw::PaletteNodeType::branch);
    REQUIRE(node.strref != ~0);
    REQUIRE(node.children.size() > 0);

    node = node.children[0];
    REQUIRE(node.type == nw::PaletteNodeType::category);
    REQUIRE(node.children.size() > 0);

    node = node.children[0];
    REQUIRE(node.type == nw::PaletteNodeType::blueprint);
    REQUIRE(node.resref == "nw_battdevour");
    REQUIRE(node.cr >= 11.0f);
    REQUIRE(node.faction == "Hostile");

    nw::Gff g2{"test_data/creaturepal.itp"};
    nw::Palette p2{g2.toplevel()};
    REQUIRE(p2.valid());
    REQUIRE(p2.restype == nw::ResourceType::utc);
}

TEST_CASE("Loading Tileset Palette", "[objects]")
{
    nw::Gff g{"test_data/tde01palstd.itp"};
    nw::Palette c{g.toplevel()};
    REQUIRE(c.valid());
    REQUIRE(c.root.children.size() > 0);
}
