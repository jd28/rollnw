#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>

TEST_CASE("objects manager", "[kernel]")
{
    auto ent = nw::kernel::objects().load("nw_chicken", nw::ObjectType::creature);
    REQUIRE(nw::kernel::objects().valid(ent));

    auto cre = ent.get<nw::Creature>();
    auto common = ent.get<nw::Common>();
    auto appearance = ent.get<nw::Appearance>();
    auto stats = ent.get<nw::CreatureStats>();
    auto scripts = ent.get<nw::CreatureScripts>();

    REQUIRE(cre);
    REQUIRE(common->resref == "nw_chicken");
    REQUIRE(stats->abilities[2] == 8);
    REQUIRE(scripts->on_attacked == "nw_c2_default5");
    REQUIRE(appearance->id == 31);
    REQUIRE(cre->gender == 1);

    nw::kernel::objects().destroy(ent);
    REQUIRE_FALSE(ent.is_alive());
}
