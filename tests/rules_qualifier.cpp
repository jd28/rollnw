#include <gtest/gtest.h>

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/rules/system.hpp>

namespace nwk = nw::kernel;

TEST(Qualifier, Basic)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto qual1 = nw::qualifier_ability(nwn1::ability_strength, 0, 20); // less than 20 str.
    EXPECT_FALSE(nw::kernel::rules().match(qual1, ent));

    auto qual2 = nw::qualifier_ability(nwn1::ability_constitution, 15, 20); // between 15 and 20
    EXPECT_TRUE(nw::kernel::rules().match(qual2, ent));

    auto qual3 = nw::qualifier_skill(nwn1::skill_discipline, 35); // at least 35
    EXPECT_TRUE(nw::kernel::rules().match(qual3, ent));
}

TEST(Qualifier, Race)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto qual1 = nw::qualifier_race(nwn1::racial_type_human);
    EXPECT_TRUE(nw::kernel::rules().match(qual1, ent));

    auto qual2 = nw::qualifier_race(nwn1::racial_type_ooze);
    EXPECT_FALSE(nw::kernel::rules().match(qual2, ent));
}

TEST(Qualifier, Level)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto qual1 = nw::qualifier_level(0, 1);
    EXPECT_FALSE(nw::kernel::rules().match(qual1, ent));

    auto qual2 = nw::qualifier_level(1);
    EXPECT_TRUE(nw::kernel::rules().match(qual2, ent));
}

TEST(Qualifier, Alignment)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto ent2 = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(ent2);

    auto qual1 = nw::qualifier_alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::good);

    EXPECT_TRUE(nw::kernel::rules().match(qual1, ent));
    EXPECT_FALSE(nw::kernel::rules().match(qual1, ent2));

    auto qual2 = nw::qualifier_alignment(nw::AlignmentAxis::both,
        nw::AlignmentFlags::neutral);

    EXPECT_FALSE(nw::kernel::rules().match(qual2, ent));
    EXPECT_TRUE(nw::kernel::rules().match(qual2, ent2));

    auto qual3 = nw::qualifier_alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful);

    EXPECT_FALSE(nw::kernel::rules().match(qual3, ent));
    EXPECT_FALSE(nw::kernel::rules().match(qual3, ent2));

    auto qual4 = nw::qualifier_alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::chaotic);

    EXPECT_FALSE(nw::kernel::rules().match(qual4, ent));
    EXPECT_FALSE(nw::kernel::rules().match(qual4, ent2));

    auto qual5 = nw::qualifier_alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful | nw::AlignmentFlags::neutral);

    EXPECT_TRUE(nw::kernel::rules().match(qual5, ent));
    EXPECT_TRUE(nw::kernel::rules().match(qual5, ent2));

    auto qual6 = nw::qualifier_alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::evil);

    EXPECT_FALSE(nw::kernel::rules().match(qual6, ent));
    EXPECT_FALSE(nw::kernel::rules().match(qual6, ent2));

    auto qual7 = nw::qualifier_alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::neutral | nw::AlignmentFlags::good);

    EXPECT_TRUE(nw::kernel::rules().match(qual7, ent));
    EXPECT_TRUE(nw::kernel::rules().match(qual7, ent2));

    auto qual8 = nw::qualifier_alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::neutral);

    EXPECT_TRUE(nw::kernel::rules().match(qual8, ent));
    EXPECT_TRUE(nw::kernel::rules().match(qual8, ent2));
}

TEST(Qualifier, ClassLevel)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto qual1 = nw::qualifier_class_level(nwn1::class_type_fighter, 30, 40);
    EXPECT_FALSE(nw::kernel::rules().match(qual1, ent));

    auto qual2 = nw::qualifier_class_level(nwn1::class_type_fighter, 10);
    EXPECT_TRUE(nw::kernel::rules().match(qual2, ent));

    auto qual3 = nw::qualifier_class_level(nwn1::class_type_fighter, 1, 1);
    EXPECT_FALSE(nw::kernel::rules().match(qual3, ent));

    auto qual4 = nw::qualifier_class_level(nwn1::class_type_fighter, 4, 5);
    EXPECT_FALSE(nw::kernel::rules().match(qual4, ent));
}
