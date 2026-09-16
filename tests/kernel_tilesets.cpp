#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/log.hpp>

#include <array>

namespace nwk = nw::kernel;

TEST(KernelTilesets, Load)
{
    auto* tileset = nwk::tilesets().load("TTR01");
    ASSERT_TRUE(tileset);
    ASSERT_FALSE(tileset->tiles.empty());
    EXPECT_FALSE(tileset->tiles.front().image_map_2d.empty());
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
    ASSERT_EQ(tileset->terrains.size(), 3u);
    EXPECT_EQ(tileset->terrains[0].name, "Grass");
    EXPECT_EQ(tileset->terrains[1].name, "Water");
    EXPECT_EQ(tileset->terrains[2].name, "Trees");
    ASSERT_EQ(tileset->crossers.size(), 4u);
    EXPECT_EQ(tileset->crossers[0].name, "Stream");
    ASSERT_EQ(tileset->tile_topologies.size(), tileset->tiles.size());
    ASSERT_TRUE(tileset->tile_topologies[0].valid);
    EXPECT_EQ(tileset->tile_topologies[0].terrain,
        (std::array<int32_t, 4>{0, 0, 0, 0}));
    EXPECT_EQ(tileset->tile_topologies[0].height,
        (std::array<int32_t, 4>{1, 1, 0, 1}));
    EXPECT_EQ(tileset->tile_topologies[0].crosser,
        (std::array<int32_t, 4>{-1, -1, -1, -1}));
    EXPECT_EQ(tileset->default_terrain, 0);
    EXPECT_TRUE(tileset->has_height_transition);
    ASSERT_EQ(tileset->groups.size(), 67u);
    EXPECT_EQ(tileset->groups[0].rows, 1u);
    EXPECT_EQ(tileset->groups[0].columns, 1u);
    EXPECT_EQ(tileset->group_tile_ids[tileset->groups[0].tile_offset], 118);
    EXPECT_EQ(tileset->grouped_tiles[118], 1u);
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
    ASSERT_EQ(tileset->tile_topologies.size(), 1u);
    EXPECT_FALSE(tileset->tile_topologies.front().valid);
}
