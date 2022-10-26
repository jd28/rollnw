#include <catch2/catch_all.hpp>

#include <nw/formats/Palette.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("palette: load creature", "[objects]")
{
    nw::Gff g{"test_data/user/scratch/creaturepalstd.itp"};
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

    nw::Gff g2{"test_data/user/scratch/creaturepal.itp"};
    nw::Palette p2{g2};
    REQUIRE(p2.valid());
    REQUIRE(p2.resource_type == nw::ResourceType::utc);
}

TEST_CASE("palette: load tileset", "[objects]")
{
    nw::Gff g{"test_data/user/scratch/tde01palstd.itp"};
    nw::Palette c{g};
    REQUIRE(c.valid());
    REQUIRE(c.root.children.size() > 0);
}

TEST_CASE("palette: to json")
{
    nw::Gff g{"test_data/user/scratch/tde01palstd.itp"};
    nw::Palette c{g};
    REQUIRE(c.valid());
    auto j = c.to_json(nw::ResourceType::set);
    std::ofstream f{"tmp/tde01palstd.itp.json"};
    f << std::setw(4) << j;

    nw::Gff g2{"test_data/user/scratch/creaturepalstd.itp"};
    nw::Palette c2{g2};
    auto j2 = c2.to_json(nw::ResourceType::utc);
    std::ofstream f2{"tmp/creaturepalstd.itp.json"};
    f2 << std::setw(4) << j2;
}
