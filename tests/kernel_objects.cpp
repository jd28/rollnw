#include <gtest/gtest.h>

#include <nw/functions.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Player.hpp>

#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>

#include <filesystem>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

using namespace std::literals;

TEST(ObjectSystem, LoadCreature)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>("nw_chicken");

    EXPECT_TRUE(ent);
    EXPECT_EQ(ent->common.resref, "nw_chicken");
    EXPECT_EQ(ent->stats.get_ability_score(nwn1::ability_dexterity), 7);
    EXPECT_EQ(ent->scripts.on_attacked, "nw_c2_default5");
    EXPECT_EQ(*ent->appearance.id, 31);
    EXPECT_EQ(ent->gender, 1);

    auto ent2 = nwk::objects().get<nw::Creature>(ent->handle());
    EXPECT_EQ(ent, ent2);

    auto handle = ent->handle();
    nwk::objects().destroy(handle);
    EXPECT_FALSE(nwk::objects().valid(handle));

    auto ent3 = nwk::objects().load_file<nw::Creature>("test_data/user/scratch/pl_agent_001.utc.json");

    EXPECT_TRUE(ent3);
    EXPECT_EQ(ent3->common.resref, "pl_agent_001");
    EXPECT_EQ(ent3->stats.get_ability_score(nwn1::ability_dexterity), 13);
    EXPECT_EQ(ent3->scripts.on_attacked, "mon_ai_5attacked");
    EXPECT_EQ(*ent3->appearance.id, 6);
    EXPECT_EQ(ent3->appearance.body_parts.shin_left, 1);
    EXPECT_EQ(ent3->soundset, 171);
    EXPECT_TRUE(nwn1::get_equipped_item(ent3, nw::EquipIndex::chest));
    EXPECT_EQ(ent3->combat_info.ac_natural_bonus, 0);
    EXPECT_EQ(ent3->combat_info.special_abilities.size(), 1);
    EXPECT_EQ(ent3->combat_info.special_abilities[0].spell, nw::Spell::make(120));
}

TEST(ObjectSystem, ObjectByTag)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    std::vector<nw::Creature*> chickens;
    for (size_t i = 0; i < 10; ++i) {
        chickens.push_back(nwk::objects().load<nw::Creature>("nw_chicken"));
    }

    EXPECT_EQ(chickens[0]->tag().view(), "NW_CHICKEN");
    EXPECT_TRUE(nwk::objects().get_by_tag("NW_CHICKEN"));
    EXPECT_TRUE(nwk::objects().get_by_tag("NW_CHICKEN", 5));
    EXPECT_FALSE(nwk::objects().get_by_tag("NW_CHICKEN", 100));

    for (auto chicken : chickens) {
        nwk::objects().destroy(chicken->handle());
    }

    EXPECT_FALSE(nwk::objects().get_by_tag("NW_CHICKEN"));
}

TEST(ObjectSystem, LoadPlayer)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testmonkpc");
    EXPECT_TRUE(pl);

    auto pl2 = nwk::objects().load_player("WRONG", "testmonkpc");
    EXPECT_FALSE(pl2);

    auto pl3 = nwk::objects().load_file<nw::Player>(fs::path("test_data/user/servervault/CDKEY/testmonkpc.bic"));
    EXPECT_TRUE(pl3);
}
