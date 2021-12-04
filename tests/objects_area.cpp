#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Area.hpp>

TEST_CASE("Loading area", "[objects]")
{
    nw::Gff are{"test_data/test_area.are"};
    nw::Gff git{"test_data/test_area.git"};
    nw::Gff gic{"test_data/test_area.gic"};

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
