#include <catch2/catch.hpp>

#include <nw/components/Area.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("area: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().make<nw::Area>();
    nw::GffInputArchive are{"test_data/user/development/test_area.are"};
    REQUIRE(are.valid());
    nw::GffInputArchive git{"test_data/user/development/test_area.git"};
    REQUIRE(git.valid());
    nw::GffInputArchive gic{"test_data/user/development/test_area.gic"};
    REQUIRE(gic.valid());

    nw::Area::deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());

    REQUIRE(ent->tileset == "ttf02");
    REQUIRE(!(ent->flags & nw::AreaFlags::interior));
    REQUIRE(!(ent->flags & nw::AreaFlags::natural));
    REQUIRE(ent->tiles.size() > 0);
    REQUIRE(ent->tiles[0].id == 68);
    REQUIRE(ent->height == 16);
    REQUIRE(ent->width == 16);

    REQUIRE(ent->creatures.size() > 0);
    REQUIRE(ent->creatures[0]->common.resref == "test_creature");
    REQUIRE(ent->creatures[0]->stats.abilities[0] == 20);
}

TEST_CASE("area: json roundtrip", "[objects]")
{
    auto ent = new nw::Area;
    nw::GffInputArchive are{"test_data/user/development/test_area.are"};
    REQUIRE(are.valid());
    nw::GffInputArchive git{"test_data/user/development/test_area.git"};
    REQUIRE(git.valid());
    nw::GffInputArchive gic{"test_data/user/development/test_area.gic"};
    REQUIRE(gic.valid());

    nw::Area::deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());

    nlohmann::json j;
    nw::Area::serialize(ent, j);

    auto ent2 = new nw::Area;
    nw::Area::deserialize(ent2, j);
    REQUIRE(ent2);

    nlohmann::json j2;
    nw::Area::serialize(ent2, j2);

    REQUIRE(j == j2);
}
