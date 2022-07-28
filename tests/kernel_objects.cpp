#include <catch2/catch.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>

TEST_CASE("objects manager", "[kernel]")
{
    auto ent = nw::kernel::objects().load<nw::Creature>("nw_chicken"sv);

    REQUIRE(ent);
    REQUIRE(ent->common.resref == "nw_chicken");
    REQUIRE(ent->stats.abilities[2] == 8);
    REQUIRE(ent->scripts.on_attacked == "nw_c2_default5");
    REQUIRE(ent->appearance.id == 31);
    REQUIRE(ent->gender == 1);

    // nw::kernel::objects().destroy(ent);
    // REQUIRE_FALSE(ent.is_alive());
}
