#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/system.hpp>

TEST_CASE("qualifier", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    const auto ability_strength = nw::kernel::rules().get_constant("ABILITY_STRENGTH");
    const auto ability_con = nw::kernel::rules().get_constant("ABILITY_CONSTITUTION");
    const auto skill_tumble = nw::kernel::rules().get_constant("SKILL_TUMBLE");
    const auto skill_disc = nw::kernel::rules().get_constant("SKILL_DISCIPLINE");

    auto qual1 = nw::qualifier::ability(ability_strength, 0, 20); // less than 20 str.
    REQUIRE_FALSE(qual1.match(c));

    auto qual2 = nw::qualifier::ability(ability_con, 15, 20); // between 15 and 20
    REQUIRE(qual2.match(c));

    auto qual3 = nw::qualifier::skill(skill_disc, 35); // at least 35
    REQUIRE(qual3.match(c));

    nw::kernel::unload_module();
}

TEST_CASE("qualifier: race", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    const auto race_human = nw::kernel::rules().get_constant("RACIAL_TYPE_HUMAN");
    const auto race_ooze = nw::kernel::rules().get_constant("RACIAL_TYPE_OOZE");

    auto qual1 = nw::qualifier::race(race_human);
    REQUIRE(qual1.match(c));

    auto qual2 = nw::qualifier::race(race_ooze);
    REQUIRE_FALSE(qual2.match(c));

    nw::kernel::unload_module();
}
