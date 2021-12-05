#include <catch2/catch.hpp>

#include <nw/objects/Palette.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading Creature Palette", "[objects]")
{
    nw::GffInputArchive g{"test_data/creaturepalstd.itp"};
    nw::Palette c{g.toplevel()};
    REQUIRE(c.root.children.size() > 0);
    auto node = c.root.children[0];

    REQUIRE(node.type == nw::PaletteNodeType::branch);
    REQUIRE(node.strref != ~0);
    REQUIRE(node.children.size() > 0);

    auto catnode = node.children[0];
    REQUIRE(catnode.type == nw::PaletteNodeType::category);
    REQUIRE(catnode.children.size() > 0);

    auto bluenode = catnode.children[0];
    REQUIRE(bluenode.type == nw::PaletteNodeType::blueprint);
    REQUIRE(bluenode.resref == "nw_battdevour");
    REQUIRE(bluenode.cr >= 11.0f);
    REQUIRE(bluenode.faction == "Hostile");

    nw::GffInputArchive g2{"test_data/creaturepal.itp"};
    nw::Palette p2{g2.toplevel()};
    REQUIRE(p2.valid());
    REQUIRE(p2.restype == nw::ResourceType::utc);
}

TEST_CASE("Loading Tileset Palette", "[objects]")
{
    nw::GffInputArchive g{"test_data/tde01palstd.itp"};
    nw::Palette c{g.toplevel()};
    REQUIRE(c.valid());
    REQUIRE(c.root.children.size() > 0);
}
