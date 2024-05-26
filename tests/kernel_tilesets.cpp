#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/log.hpp>

namespace nwk = nw::kernel;

TEST(KernelTilesets, Load)
{
    EXPECT_TRUE(nwk::tilesets().load("TTR01"));
    EXPECT_TRUE(nwk::tilesets().get("TTR01"));
    EXPECT_FALSE(nwk::tilesets().load("FAKE01"));
    EXPECT_FALSE(nwk::tilesets().get("FAKE01"));
}
