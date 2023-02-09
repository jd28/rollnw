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
#include <nw/serialization/Archives.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/combat.hpp>
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

    nwk::unload_module();
}

TEST(Player, AttackBonus)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testbardrddpc1");
    EXPECT_TRUE(pl);
    EXPECT_EQ(nwn1::base_attack_bonus(pl), 10);

    nwk::unload_module();
}

#ifdef ROLLNW_ENABLE_LEGACY

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

    nwk::unload_module();
}

#endif // ROLLNW_ENABLE_LEGACY
