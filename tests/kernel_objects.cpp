#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>

TEST_CASE("objects manager", "[kernel]")
{
    auto obj = nw::kernel::objects().make("nw_chicken", nw::ObjectType::creature);
    REQUIRE(nw::kernel::objects().valid(obj));

    auto c = obj.get<nw::Creature>();
    REQUIRE(c);
    REQUIRE(c->common()->resref == "nw_chicken");
    REQUIRE(c->stats.abilities[2] == 8);
    REQUIRE(c->scripts.on_attacked == "nw_c2_default5");
    REQUIRE(c->appearance.id == 31);
    REQUIRE(c->gender == 1);

    nw::kernel::objects().destroy(obj);
    REQUIRE_FALSE(obj.is_alive());
}
