#include <gtest/gtest.h>

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/system.hpp>

namespace {

int32_t available_feat_count_from_script(const nw::Creature* creature)
{
    if (!creature) { return 0; }

    nw::Vector<nw::smalls::Value> count_args;
    const auto count = nwn1::bridge::call_nwn1_module_int("nwn1.feats", "count", count_args).value_or(0);
    if (count <= 0) {
        return 0;
    }

    int32_t result = 0;
    nw::Vector<nw::smalls::Value> args;
    args.reserve(2);
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(0));

    for (int32_t i = 0; i < count; ++i) {
        args[1] = nw::smalls::Value::make_int(i);
        if (nwn1::bridge::call_nwn1_module_bool("nwn1.feats", "can_take_feat", args).value_or(false)) {
            ++result;
        }
    }

    return result;
}

} // namespace

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Requirement, Basic)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    nw::Requirement req{{
        nw::qualifier_ability(nwn1::ability_strength, nw::QualifierMatch::lte, 20),
        nw::qualifier_ability(nwn1::ability_constitution, 15),
        nw::qualifier_ability(nwn1::ability_constitution, nw::QualifierMatch::lte, 20),
        nw::qualifier_skill(nwn1::skill_discipline, 35),
    }};

    EXPECT_FALSE(nwk::rules().meets_requirement(req, ent));

    nw::Requirement req2{{
        nw::qualifier_ability(nwn1::ability_constitution, 15),
        nw::qualifier_ability(nwn1::ability_constitution, nw::QualifierMatch::lte, 20),
        nw::qualifier_skill(nwn1::skill_discipline, 35),
    }};

    EXPECT_TRUE(nwk::rules().meets_requirement(req2, ent));

    nw::Requirement req3{{nw::qualifier_ability(nwn1::ability_constitution, 15),
                             nw::qualifier_ability(nwn1::ability_strength, nw::QualifierMatch::lte, 20),
                             nw::qualifier_skill(nwn1::skill_discipline, 35)},
        false};

    EXPECT_TRUE(nwk::rules().meets_requirement(req3, ent));
}

TEST(Requirement, Feat)
{
    EXPECT_EQ(available_feat_count_from_script(nullptr), 0);

    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    EXPECT_GT(available_feat_count_from_script(ent), 0);
}
