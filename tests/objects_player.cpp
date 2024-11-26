#include <gtest/gtest.h>

#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Player.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/constants.hpp>
#include <nwn1/effects.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Player, LevelHistory)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testsorcpc1");
    EXPECT_TRUE(pl);
    EXPECT_EQ(pl->history.entries.size(), 15);
    EXPECT_EQ(pl->history.entries[0].known_spells.size(), 6);
}

TEST(Player, AttackBonus)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testbardrddpc1");
    EXPECT_TRUE(pl);
    EXPECT_EQ(nwn1::base_attack_bonus(pl), 10);
}

TEST(Player, GffJsonSerialize)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ba = nwk::resman().demand_server_vault("CDKEY", "testwizardpc1");
    nw::Gff g{std::move(ba)};
    EXPECT_TRUE(g.valid());

    auto j = nw::gff_to_gffjson(g);
    std::ofstream out{"tmp/testwizardpc1.bic.gffjson"};
    out << std::setw(4) << j;
}

TEST(Player, Colors)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    EXPECT_EQ(pl->appearance.colors[nw::CreatureColors::hair], 20);
    EXPECT_EQ(pl->appearance.colors[nw::CreatureColors::skin], 2);
}

TEST(Player, Inventory)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);

    EXPECT_EQ(pl->inventory.size(), 4);
    auto slot1 = pl->inventory.find_slot(1, 1);
    auto [x1, y1] = pl->inventory.slot_to_xy(slot1);
    EXPECT_EQ(x1, 1);
    EXPECT_EQ(y1, 0);

    auto slot2 = pl->inventory.find_slot(3, 4);
    auto [x2, y2] = pl->inventory.slot_to_xy(slot2);
    EXPECT_EQ(x2, 5);
    EXPECT_EQ(y2, 0);

    auto it = pl->inventory.items[0].item.as<nw::Item*>();
    EXPECT_TRUE(pl->inventory.remove_item(it));
    EXPECT_FALSE(pl->inventory.has_item(it));
    EXPECT_EQ(pl->inventory.size(), 3);
    EXPECT_TRUE(pl->inventory.can_add_item(it));
    EXPECT_TRUE(pl->inventory.add_item(it));
    EXPECT_TRUE(pl->inventory.has_item(it));
    EXPECT_EQ(pl->inventory.size(), 4);
}

TEST(Player, PerPartColor)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ba = nwk::resman().demand_server_vault("CDKEY", "damienoneknife");
    nw::Gff g{std::move(ba)};
    EXPECT_TRUE(g.valid());

    auto j = nw::gff_to_gffjson(g);
    std::ofstream out{"tmp/damienoneknife.bic.gffjson"};
    out << std::setw(4) << j;

    auto pl = nwk::objects().load_player("CDKEY", "damienoneknife");
    EXPECT_TRUE(pl);
}

TEST(Player, Portrait)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);

    EXPECT_EQ(pl->appearance.portrait.view(), "po_dw_m_02_");
}

TEST(Player, GffSerialization)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    auto oa = nw::serialize(pl);
    oa.write_to("tmp/daeris1.bic");
}

TEST(Player, JsonSerialization)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "daeris1");
    EXPECT_TRUE(pl);
    auto oa = nw::serialize(pl);
    oa.write_to("tmp/daeris1.bic");

    nlohmann::json j;
    nw::Player::serialize(pl, j);

    std::ofstream f{"tmp/daeris1.bic.json"};
    f << std::setw(4) << j;
}
