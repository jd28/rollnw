#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/rules/system.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("requirement", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    nw::Requirement req{{
        nwn1::qual::ability(nwn1::ability_strength, 0, 20),
        nwn1::qual::ability(nwn1::ability_constitution, 15, 20),
        nwn1::qual::skill(nwn1::skill_discipline, 35),
    }};

    REQUIRE_FALSE(nwk::rules().meets_requirement(req, ent));

    nw::Requirement req2{{
        nwn1::qual::ability(nwn1::ability_constitution, 15, 20),
        nwn1::qual::skill(nwn1::skill_discipline, 35),
    }};

    REQUIRE(nwk::rules().meets_requirement(req2, ent));

    nw::Requirement req3{{nwn1::qual::ability(nwn1::ability_constitution, 15, 20),
                             nwn1::qual::ability(nwn1::ability_strength, 0, 20),
                             nwn1::qual::skill(nwn1::skill_discipline, 35)},
        false};

    REQUIRE(nwk::rules().meets_requirement(req3, ent));
}

TEST_CASE("requirement: feats", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto feats = nw::get_all_available_feats(ent);
    REQUIRE(feats.size() > 0);
}
