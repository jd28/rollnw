#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/system.hpp>

TEST_CASE("requirement", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    const auto ability_strength = nw::kernel::rules().get_constant("ABILITY_STRENGTH");
    const auto ability_con = nw::kernel::rules().get_constant("ABILITY_CONSTITUTION");
    const auto skill_tumble = nw::kernel::rules().get_constant("SKILL_TUMBLE");
    const auto skill_disc = nw::kernel::rules().get_constant("SKILL_DISCIPLINE");

    nw::Requirement req{{
        nw::qualifier::ability(ability_strength, 0, 20),
        nw::qualifier::ability(ability_con, 15, 20),
        nw::qualifier::skill(skill_disc, 35),
    }};

    REQUIRE_FALSE(req.met(c));

    nw::Requirement req2{{
        nw::qualifier::ability(ability_con, 15, 20),
        nw::qualifier::skill(skill_disc, 35),
    }};

    REQUIRE(req2.met(c));

    nw::Requirement req3{{nw::qualifier::ability(ability_con, 15, 20),
                             nw::qualifier::ability(ability_strength, 0, 20),
                             nw::qualifier::skill(skill_disc, 35)},
        false};

    REQUIRE(req3.met(c));

    nw::kernel::unload_module();
}
