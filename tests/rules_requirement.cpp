#include <gtest/gtest.h>

#include <nw/functions.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/system.hpp>
#include <nwn1/Profile.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Requirement, Basic)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    nw::Requirement req{{
        nwn1::qual::ability(nwn1::ability_strength, 0, 20),
        nwn1::qual::ability(nwn1::ability_constitution, 15, 20),
        nwn1::qual::skill(nwn1::skill_discipline, 35),
    }};

    EXPECT_FALSE(nwk::rules().meets_requirement(req, ent));

    nw::Requirement req2{{
        nwn1::qual::ability(nwn1::ability_constitution, 15, 20),
        nwn1::qual::skill(nwn1::skill_discipline, 35),
    }};

    EXPECT_TRUE(nwk::rules().meets_requirement(req2, ent));

    nw::Requirement req3{{nwn1::qual::ability(nwn1::ability_constitution, 15, 20),
                             nwn1::qual::ability(nwn1::ability_strength, 0, 20),
                             nwn1::qual::skill(nwn1::skill_discipline, 35)},
        false};

    EXPECT_TRUE(nwk::rules().meets_requirement(req3, ent));
    nw::kernel::unload_module();
}

TEST(Requirement, Feat)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto feats = nw::get_all_available_feats(ent);
    EXPECT_TRUE(feats.size() > 0);
    nw::kernel::unload_module();
}
