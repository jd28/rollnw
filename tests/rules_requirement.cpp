#include <gtest/gtest.h>

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/system.hpp>
#include <nw/scriptapi.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Requirement, Basic)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    nw::Requirement req{{
        nw::qualifier_ability(nwn1::ability_strength, 0, 20),
        nw::qualifier_ability(nwn1::ability_constitution, 15, 20),
        nw::qualifier_skill(nwn1::skill_discipline, 35),
    }};

    EXPECT_FALSE(nwk::rules().meets_requirement(req, ent));

    nw::Requirement req2{{
        nw::qualifier_ability(nwn1::ability_constitution, 15, 20),
        nw::qualifier_skill(nwn1::skill_discipline, 35),
    }};

    EXPECT_TRUE(nwk::rules().meets_requirement(req2, ent));

    nw::Requirement req3{{nw::qualifier_ability(nwn1::ability_constitution, 15, 20),
                             nw::qualifier_ability(nwn1::ability_strength, 0, 20),
                             nw::qualifier_skill(nwn1::skill_discipline, 35)},
        false};

    EXPECT_TRUE(nwk::rules().meets_requirement(req3, ent));
}

TEST(Requirement, Feat)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto feats = nw::get_all_available_feats(ent);
    EXPECT_TRUE(feats.size() > 0);
}
