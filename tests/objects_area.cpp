#include <catch2/catch.hpp>

#include <nw/components/Area.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("area: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().make(nw::ObjectType::area);
    nw::GffInputArchive are{"test_data/user/development/test_area.are"};
    REQUIRE(are.valid());
    nw::GffInputArchive git{"test_data/user/development/test_area.git"};
    REQUIRE(git.valid());
    nw::GffInputArchive gic{"test_data/user/development/test_area.gic"};
    REQUIRE(gic.valid());

    nw::Area::deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());
    auto area = ent.get<nw::Area>();

    REQUIRE(area->tileset == "ttf02");
    REQUIRE(!(area->flags & nw::AreaFlags::interior));
    REQUIRE(!(area->flags & nw::AreaFlags::natural));
    REQUIRE(area->tiles.size() > 0);
    REQUIRE(area->tiles[0].id == 68);
    REQUIRE(area->height == 16);
    REQUIRE(area->width == 16);

    REQUIRE(area->creatures.size() > 0);
    REQUIRE(area->creatures[0].get<nw::Common>()->resref == "test_creature");
    REQUIRE(area->creatures[0].get<nw::CreatureStats>()->abilities[0] == 20);
}

TEST_CASE("area: json roundtrip", "[objects]")
{
    auto ent = nw::kernel::objects().make(nw::ObjectType::area);
    nw::GffInputArchive are{"test_data/user/development/test_area.are"};
    REQUIRE(are.valid());
    nw::GffInputArchive git{"test_data/user/development/test_area.git"};
    REQUIRE(git.valid());
    nw::GffInputArchive gic{"test_data/user/development/test_area.gic"};
    REQUIRE(gic.valid());

    nw::Area::deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make(nw::ObjectType::area);
    nw::Area::deserialize(ent2, j);
    REQUIRE(ent2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);
}
