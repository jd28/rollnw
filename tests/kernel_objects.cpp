#include "nw/serialization/Serialization.hpp"
#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Player.hpp>
#include <nwn1/Profile.hpp>

#include <filesystem>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(ObjectSystem, LoadCreature)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>("nw_chicken"sv);

    EXPECT_TRUE(ent);
    EXPECT_EQ(ent->common.resref, "nw_chicken");
    EXPECT_EQ(ent->stats.get_ability_score(nwn1::ability_dexterity), 7);
    EXPECT_EQ(ent->scripts.on_attacked, "nw_c2_default5");
    EXPECT_EQ(ent->appearance.id, 31);
    EXPECT_EQ(ent->gender, 1);

    auto ent2 = nwk::objects().get<nw::Creature>(ent->handle());
    EXPECT_EQ(ent, ent2);

    auto handle = ent->handle();
    nwk::objects().destroy(handle);
    EXPECT_FALSE(nwk::objects().valid(handle));

    auto ent3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/scratch/pl_agent_001.utc.json"));

    EXPECT_TRUE(ent3);
    EXPECT_EQ(ent3->common.resref, "pl_agent_001");
    EXPECT_EQ(ent3->stats.get_ability_score(nwn1::ability_dexterity), 13);
    EXPECT_EQ(ent3->scripts.on_attacked, "mon_ai_5attacked");
    EXPECT_EQ(ent3->appearance.id, 6);
    EXPECT_EQ(ent3->appearance.body_parts.shin_left, 1);
    EXPECT_EQ(ent3->soundset, 171);
    EXPECT_TRUE(std::get<nw::Item*>(ent3->equipment.equips[1]));
    EXPECT_EQ(ent3->combat_info.ac_natural_bonus, 0);
    EXPECT_EQ(ent3->combat_info.special_abilities.size(), 1);
    EXPECT_EQ(ent3->combat_info.special_abilities[0].spell, 120);

    auto handle2 = ent3->handle();

    EXPECT_EQ(handle.id, handle2.id);

    nwk::unload_module();
}

TEST(ObjectSystem, LoadPlayer)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testmonkpc");
    EXPECT_TRUE(pl);

    auto pl2 = nwk::objects().load_player("WRONG", "testmonkpc");
    EXPECT_FALSE(pl2);

    auto pl3 = nwk::objects().load<nw::Player>(fs::path("test_data/user/servervault/CDKEY/testmonkpc.bic"));
    EXPECT_TRUE(pl3);

    nwk::unload_module();
}
