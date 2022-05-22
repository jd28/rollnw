#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/rules/system.hpp>

namespace fs = std::filesystem;

TEST_CASE("requirement", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    nw::Requirement req{{
        nw::qualifier::ability(nwn1::ability_strength, 0, 20),
        nw::qualifier::ability(nwn1::ability_constitution, 15, 20),
        nw::qualifier::skill(nwn1::skill_discipline, 35),
    }};

    REQUIRE_FALSE(req.met(ent));

    nw::Requirement req2{{
        nw::qualifier::ability(nwn1::ability_constitution, 15, 20),
        nw::qualifier::skill(nwn1::skill_discipline, 35),
    }};

    REQUIRE(req2.met(ent));

    nw::Requirement req3{{nw::qualifier::ability(nwn1::ability_constitution, 15, 20),
                             nw::qualifier::ability(nwn1::ability_strength, 0, 20),
                             nw::qualifier::skill(nwn1::skill_discipline, 35)},
        false};

    REQUIRE(req3.met(ent));

    nw::kernel::unload_module();
}
