#include <gtest/gtest.h>

#include "nw/kernel/FactionSystem.hpp"

#include <limits>

namespace nwk = nw::kernel;

TEST(Kernel, FactionSystem)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto num = nwk::factions().count();
    EXPECT_EQ(num, 5);

    auto name1 = nwk::factions().name(0);
    EXPECT_EQ(name1, "PC");

    auto name2 = nwk::factions().name(1000);
    EXPECT_EQ(name2, "");

    auto id1 = nwk::factions().faction_id("PC");
    EXPECT_EQ(id1, 0);

    auto id2 = nwk::factions().faction_id("nonextant");
    EXPECT_EQ(id2, std::numeric_limits<uint32_t>::max());

    auto id3 = nwk::factions().faction_id("Hostile");
    EXPECT_EQ(id3, 1);

    auto rep1 = nwk::factions().locate(0, id3);
    EXPECT_EQ(rep1.faction_1, 0);
    EXPECT_EQ(rep1.faction_2, 1);
    EXPECT_EQ(rep1.reputation, 0);

    auto rep2 = nwk::factions().reputation(0, id3);
    EXPECT_EQ(rep2, 0);

    auto id4 = nwk::factions().faction_id("Defender");
    EXPECT_EQ(id4, 4);

    auto rep3 = nwk::factions().locate(4, 2);
    EXPECT_EQ(rep3.faction_1, 4);
    EXPECT_EQ(rep3.faction_2, 2);
    EXPECT_EQ(rep3.reputation, 50);

    auto all = nwk::factions().all();
    EXPECT_EQ(all.size(), 5);
    EXPECT_EQ(all[id4], "Defender");

    nwk::unload_module();
}
