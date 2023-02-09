#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/system.hpp>
#include <nwn1/Profile.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Selector, Basic)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto sel1 = nwn1::sel::ability(nwn1::ability_strength);
    EXPECT_TRUE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    auto sel2 = nwn1::sel::ability(nwn1::ability_constitution);
    EXPECT_TRUE(nwk::rules().select(sel2, ent).as<int32_t>() == 16);

    auto sel3 = nwn1::sel::skill(nwn1::skill_tumble);
    EXPECT_TRUE(nwk::rules().select(sel3, ent).as<int32_t>() == 6);

    auto sel4 = nwn1::sel::skill(nwn1::skill_discipline);
    EXPECT_TRUE(sel4.type == nw::SelectorType::skill);
    EXPECT_TRUE(sel4.subtype.is<int32_t>());
    EXPECT_TRUE(sel4.subtype.as<int32_t>() == 3);
    EXPECT_TRUE(nwk::rules().select(sel4, ent).as<int32_t>() == 58);

    nw::kernel::unload_module();
}

TEST(Selector, Level)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto sel1 = nwn1::sel::level();
    EXPECT_TRUE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST(Selector, Race)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto sel1 = nwn1::sel::race();
    EXPECT_TRUE(nwk::rules().select(sel1, ent).as<int32_t>() == *nwn1::racial_type_human);

    nw::kernel::unload_module();
}

TEST(Selector, Feat)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto sel1 = nwn1::sel::feat(nwn1::feat_improved_critical_creature);
    EXPECT_TRUE(nwk::rules().select(sel1, ent).as<int32_t>());

    nw::kernel::unload_module();
}

TEST(Selector, ClassLevel)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto sel1 = nwn1::sel::class_level(nwn1::class_type_fighter);
    EXPECT_TRUE(nwk::rules().select(sel1, ent).as<int32_t>() == 10);

    nw::kernel::unload_module();
}

TEST(Selector, Alignment)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    auto sel1 = nwn1::sel::alignment(nw::AlignmentAxis::law_chaos);
    EXPECT_TRUE(nwk::rules().select(sel1, ent).as<int32_t>() == 50);

    auto sel2 = nwn1::sel::alignment(nw::AlignmentAxis::good_evil);
    EXPECT_TRUE(nwk::rules().select(sel2, ent).as<int32_t>() == 100);

    nw::kernel::unload_module();
}
