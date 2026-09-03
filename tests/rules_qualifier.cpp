#include <gtest/gtest.h>

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/system.hpp>

namespace nwk = nw::kernel;

TEST(Attributes, AlignmentCompositeConstants)
{
    EXPECT_EQ(nw::align_chaotic_evil,
        nw::AlignmentFlags::chaotic | nw::AlignmentFlags::evil);
}

TEST(Qualifier, Basic)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto qual1 = nw::qualifier_ability(nw::Ability::make(0), nw::QualifierMatch::lte, 20); // less than 20 str.
    EXPECT_FALSE(nw::kernel::rules().match(qual1, ent));

    auto qual2 = nw::qualifier_ability(nw::Ability::make(2), 15); // at least 15 con.
    EXPECT_TRUE(nw::kernel::rules().match(qual2, ent));

    auto qual3 = nw::qualifier_ability(nw::Ability::make(2), nw::QualifierMatch::lte, 20); // at most 20 con.
    EXPECT_TRUE(nw::kernel::rules().match(qual3, ent));

    auto qual4 = nw::qualifier_skill(nw::Skill::make(3), 35); // at least 35
    EXPECT_TRUE(nw::kernel::rules().match(qual4, ent));
}

TEST(Qualifier, UnsupportedNwnTypeIsSatisfied)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    const auto qualifier = nw::make_qualifier(
        nw::req_type_ac, -1, nw::QualifierMatch::gte, 100);
    EXPECT_TRUE(nw::kernel::rules().match(qualifier, nullptr));
}

TEST(Qualifier, Race)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto qual1 = nw::qualifier_race(nw::Race::make(6));
    EXPECT_TRUE(nw::kernel::rules().match(qual1, ent));

    auto qual2 = nw::qualifier_race(nw::Race::make(29));
    EXPECT_FALSE(nw::kernel::rules().match(qual2, ent));
}

TEST(Qualifier, Level)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    EXPECT_TRUE(ent);

    auto qual1 = nw::qualifier_level(nw::QualifierMatch::lte, 1);
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

    auto qual1 = nw::qualifier_class_level(nw::Class::make(4), 30);
    EXPECT_FALSE(nw::kernel::rules().match(qual1, ent));

    auto qual2 = nw::qualifier_class_level(nw::Class::make(4), 10);
    EXPECT_TRUE(nw::kernel::rules().match(qual2, ent));

    auto qual3 = nw::qualifier_class_level(nw::Class::make(4), nw::QualifierMatch::lte, 1);
    EXPECT_FALSE(nw::kernel::rules().match(qual3, ent));

    auto qual4 = nw::qualifier_class_level(nw::Class::make(4), nw::QualifierMatch::lte, 5);
    EXPECT_FALSE(nw::kernel::rules().match(qual4, ent));
}
