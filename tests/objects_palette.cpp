#include <catch2/catch.hpp>

#include <nw/objects/Palette.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("palette: load creature", "[objects]")
{
    nw::GffInputArchive g{"test_data/creaturepalstd.itp"};
    nw::Palette c{g};
    REQUIRE(c.root.children.size() > 0);
    auto node = c.root.children[0];

    REQUIRE(node.type == nw::PaletteNodeType::branch);
    REQUIRE(node.strref != std::numeric_limits<uint32_t>::max());
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
    nw::Palette p2{g2};
    REQUIRE(p2.valid());
    REQUIRE(p2.resource_type == nw::ResourceType::utc);
}

TEST_CASE("palette: load tileset", "[objects]")
{
    nw::GffInputArchive g{"test_data/tde01palstd.itp"};
    nw::Palette c{g};
    REQUIRE(c.valid());
    REQUIRE(c.root.children.size() > 0);
}

TEST_CASE("palette: to json")
{
    nw::GffInputArchive g{"test_data/tde01palstd.itp"};
    nw::Palette c{g};
    REQUIRE(c.valid());
    auto j = c.to_json(nw::ResourceType::set);
    std::ofstream f{"tmp/tde01palstd.itp.json"};
    f << std::setw(4) << j;

    nw::GffInputArchive g2{"test_data/creaturepalstd.itp"};
    nw::Palette c2{g2};
    auto j2 = c2.to_json(nw::ResourceType::utc);
    std::ofstream f2{"tmp/creaturepalstd.itp.json"};
    f2 << std::setw(4) << j2;
}
