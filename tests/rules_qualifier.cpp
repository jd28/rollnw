#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/system.hpp>
#include <nwn1/Profile.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Qualifier, Basic)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto qual1 = nwn1::qual::ability(nwn1::ability_strength, 0, 20); // less than 20 str.
    EXPECT_FALSE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::ability(nwn1::ability_constitution, 15, 20); // between 15 and 20
    EXPECT_TRUE(nwn1::match(qual2, ent));

    auto qual3 = nwn1::qual::skill(nwn1::skill_discipline, 35); // at least 35
    EXPECT_TRUE(nwn1::match(qual3, ent));

    nwk::unload_module();
}

TEST(Qualifier, Race)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto qual1 = nwn1::qual::race(nwn1::racial_type_human);
    EXPECT_TRUE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::race(nwn1::racial_type_ooze);
    EXPECT_FALSE(nwn1::match(qual2, ent));

    nwk::unload_module();
}

TEST(Qualifier, Level)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto qual1 = nwn1::qual::level(0, 1);
    EXPECT_FALSE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::level(1);
    EXPECT_TRUE(nwn1::match(qual2, ent));

    nwk::unload_module();
}

TEST(Qualifier, Alignment)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto ent2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(ent2);

    auto qual1 = nwn1::qual::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::good);

    EXPECT_TRUE(nwn1::match(qual1, ent));
    EXPECT_FALSE(nwn1::match(qual1, ent2));

    auto qual2 = nwn1::qual::alignment(nw::AlignmentAxis::both,
        nw::AlignmentFlags::neutral);

    EXPECT_FALSE(nwn1::match(qual2, ent));
    EXPECT_TRUE(nwn1::match(qual2, ent2));

    auto qual3 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful);

    EXPECT_FALSE(nwn1::match(qual3, ent));
    EXPECT_FALSE(nwn1::match(qual3, ent2));

    auto qual4 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::chaotic);

    EXPECT_FALSE(nwn1::match(qual4, ent));
    EXPECT_FALSE(nwn1::match(qual4, ent2));

    auto qual5 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful | nw::AlignmentFlags::neutral);

    EXPECT_TRUE(nwn1::match(qual5, ent));
    EXPECT_TRUE(nwn1::match(qual5, ent2));

    auto qual6 = nwn1::qual::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::evil);

    EXPECT_FALSE(nwn1::match(qual6, ent));
    EXPECT_FALSE(nwn1::match(qual6, ent2));

    auto qual7 = nwn1::qual::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::neutral | nw::AlignmentFlags::good);

    EXPECT_TRUE(nwn1::match(qual7, ent));
    EXPECT_TRUE(nwn1::match(qual7, ent2));

    auto qual8 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::neutral);

    EXPECT_TRUE(nwn1::match(qual8, ent));
    EXPECT_TRUE(nwn1::match(qual8, ent2));

    nwk::unload_module();
}

TEST(Qualifier, ClassLevel)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto qual1 = nwn1::qual::class_level(nwn1::class_type_fighter, 30, 40);
    EXPECT_FALSE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::class_level(nwn1::class_type_fighter, 10);
    EXPECT_TRUE(nwn1::match(qual2, ent));

    auto qual3 = nwn1::qual::class_level(nwn1::class_type_fighter, 1, 1);
    EXPECT_FALSE(nwn1::match(qual3, ent));

    auto qual4 = nwn1::qual::class_level(nwn1::class_type_fighter, 4, 5);
    EXPECT_FALSE(nwn1::match(qual4, ent));

    nwk::unload_module();
}
