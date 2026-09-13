#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/log.hpp>

namespace nwk = nw::kernel;

TEST(KernelTilesets, Load)
{
    auto* tileset = nwk::tilesets().load("TTR01");
    ASSERT_TRUE(tileset);
    ASSERT_FALSE(tileset->tiles.empty());
    EXPECT_EQ(tileset->tiles.front().path_node.size(), 1u);
    EXPECT_EQ(tileset->tiles.front().path_node_orientation % 90, 0);
    ASSERT_GT(tileset->tiles.size(), 60u);
    ASSERT_EQ(tileset->tiles[60].door_slots.size(), 1u);
    const auto& slot = tileset->tiles[60].door_slots.front();
    EXPECT_EQ(slot.type, 65);
    EXPECT_FLOAT_EQ(slot.position.x, 2.78f);
    EXPECT_FLOAT_EQ(slot.position.y, 0.0f);
    EXPECT_FLOAT_EQ(slot.position.z, 0.0f);
    EXPECT_FLOAT_EQ(slot.orientation, 270.0f);
    EXPECT_EQ(nwk::tilesets().get("TTR01"), tileset);
    EXPECT_FALSE(nwk::tilesets().load("FAKE01"));
    EXPECT_FALSE(nwk::tilesets().get("FAKE01"));
}

TEST(KernelTilesets, RejectsUnboundedDoorSlotCounts)
{
    auto* tileset = nwk::tilesets().load("bad_doors");
    ASSERT_NE(tileset, nullptr);
    ASSERT_EQ(tileset->tiles.size(), 1u);
    EXPECT_TRUE(tileset->tiles.front().door_slots.empty());
}
