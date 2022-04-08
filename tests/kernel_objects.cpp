#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>

TEST_CASE("objects manager", "[kernel]")
{
    auto obj = nw::kernel::objects().load("nw_chicken", nw::ObjectType::creature);
    REQUIRE(nw::kernel::objects().valid(obj));
    auto base = nw::kernel::objects().get(obj);
    REQUIRE(base);
    auto c = base->as_creature();
    REQUIRE(c);
    REQUIRE(c->common()->resref == "nw_chicken");
    REQUIRE(c->stats.abilities[2] == 8);
    REQUIRE(c->scripts.on_attacked == "nw_c2_default5");
    REQUIRE(c->appearance.id == 31);
    REQUIRE(c->gender == 1);

    nw::kernel::objects().destroy(obj);
    REQUIRE_FALSE(nw::kernel::objects().get(obj));

    auto obj2 = nw::kernel::objects().load("nw_chicken", nw::ObjectType::creature);
    REQUIRE(nw::kernel::objects().valid(obj2));
    REQUIRE(obj2.id == nw::ObjectID{0});
    REQUIRE(obj2.version == 1);
    REQUIRE(nw::kernel::objects().get(obj2));

    nw::kernel::objects().clear();
}
