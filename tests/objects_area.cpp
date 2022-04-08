#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("area: from_gff", "[objects]")
{
    nw::GffInputArchive are{"test_data/user/development/test_area.are"};
    nw::GffInputArchive git{"test_data/user/development/test_area.git"};
    nw::GffInputArchive gic{"test_data/user/development/test_area.gic"};

    nw::Area area{are.toplevel(), git.toplevel(), gic.toplevel()};
    REQUIRE(area.tileset == "ttf02");
    REQUIRE(!(area.flags & nw::AreaFlags::interior));
    REQUIRE(!(area.flags & nw::AreaFlags::natural));
    REQUIRE(area.tiles.size() > 0);
    REQUIRE(area.tiles[0].id == 68);
    REQUIRE(area.height == 16);
    REQUIRE(area.width == 16);

    REQUIRE(area.creatures.size() > 0);
    REQUIRE(area.creatures[0]->common()->resref == "test_creature");
    REQUIRE(area.creatures[0]->stats.abilities[0] == 20);
}

TEST_CASE("area: to_json", "[objects]")
{
    nw::GffInputArchive are{"test_data/user/development/test_area.are"};
    nw::GffInputArchive git{"test_data/user/development/test_area.git"};
    nw::GffInputArchive gic{"test_data/user/development/test_area.gic"};

    nw::Area area{are.toplevel(), git.toplevel(), gic.toplevel()};

    nlohmann::json j = area.to_json();

    std::ofstream f{"tmp/test_area.caf.json"};
    f << std::setw(4) << j;
}

TEST_CASE("area: json roundtrip", "[objects]")
{
    nw::GffInputArchive are{"test_data/user/development/test_area.are"};
    nw::GffInputArchive git{"test_data/user/development/test_area.git"};
    nw::GffInputArchive gic{"test_data/user/development/test_area.gic"};

    nw::Area area{are.toplevel(), git.toplevel(), gic.toplevel()};

    nlohmann::json j = area.to_json();
    nw::Area area2{j, {}};
    nlohmann::json j2 = area2.to_json();
    REQUIRE(j == j2);
    nw::kernel::objects().clear();
}
