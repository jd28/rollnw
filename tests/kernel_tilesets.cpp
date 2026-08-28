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
    EXPECT_EQ(nwk::tilesets().get("TTR01"), tileset);
    EXPECT_FALSE(nwk::tilesets().load("FAKE01"));
    EXPECT_FALSE(nwk::tilesets().get("FAKE01"));
}
