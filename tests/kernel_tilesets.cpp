#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace nwk = nw::kernel;

TEST(KernelTilesets, Load)
{
    auto* tileset = nwk::tilesets().load("TTR01");
    ASSERT_TRUE(tileset);
    EXPECT_EQ(tileset->strref, 1606u);
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
    EXPECT_TRUE(tileset->primary_rules_valid);
    ASSERT_EQ(tileset->primary_rules.size(), 28u);
    EXPECT_EQ(tileset->primary_rules[0],
        (nw::TilesetPrimaryRule{0, 0, 0, 0, 0, 0}));
    EXPECT_EQ(tileset->primary_rules[3],
        (nw::TilesetPrimaryRule{0, 0, 1, 1, 0, 1}));
    EXPECT_EQ(tileset->primary_rules[16],
        (nw::TilesetPrimaryRule{1, 1, 0, 0, 0, 1}));
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
    EXPECT_TRUE(tileset->primary_rules_valid);
    EXPECT_TRUE(tileset->primary_rules.empty());
}

TEST(KernelTilesets, LoadsPrimaryRulesAndFlagsMalformedTablesWithoutBreakingRendering)
{
    auto* valid = nwk::tilesets().load("rules_primary");
    ASSERT_NE(valid, nullptr);
    EXPECT_TRUE(valid->primary_rules_valid);
    ASSERT_EQ(valid->primary_rules.size(), 5u);
    EXPECT_EQ(valid->primary_rules[1],
        (nw::TilesetPrimaryRule{0, 0, 1, 0, 2, 0}));
    EXPECT_EQ(valid->primary_rules[2],
        (nw::TilesetPrimaryRule{1, 0, 0, 0, 2, 0}));
    EXPECT_EQ(valid->primary_rules[3],
        (nw::TilesetPrimaryRule{0, 0, 1, 1, 0, 1}));
    EXPECT_EQ(valid->primary_rules[4],
        (nw::TilesetPrimaryRule{1, 1, 0, 0, 0, 1}));

    auto* malformed = nwk::tilesets().load("bad_rules");
    ASSERT_NE(malformed, nullptr);
    ASSERT_EQ(malformed->tiles.size(), 1u);
    EXPECT_EQ(malformed->tiles.front().model, "tno01_r01_01");
    EXPECT_FALSE(malformed->primary_rules_valid);
    EXPECT_TRUE(malformed->primary_rules.empty());

    auto* tin01 = nwk::tilesets().load("tin01");
    ASSERT_NE(tin01, nullptr);
    EXPECT_TRUE(tin01->primary_rules_valid);
    EXPECT_EQ(tin01->primary_rules.size(), 25u);

    auto* tno01 = nwk::tilesets().load("tno01");
    ASSERT_NE(tno01, nullptr);
    EXPECT_TRUE(tno01->primary_rules_valid);
    EXPECT_EQ(tno01->primary_rules.size(), 192u);

    auto* ttf01 = nwk::tilesets().load("ttf01");
    ASSERT_NE(ttf01, nullptr);
    EXPECT_TRUE(ttf01->primary_rules_valid);
    EXPECT_EQ(ttf01->primary_rules.size(), 9u);
    for (const auto& rule : ttf01->primary_rules) {
        EXPECT_EQ(rule.adjacent, rule.changed);
        EXPECT_EQ(rule.adjacent_height, rule.changed_height);
    }

    namespace fs = std::filesystem;
    const fs::path temp_dir = fs::absolute("tmp/kernel_tilesets_primary_rules");
    fs::remove_all(temp_dir);
    fs::create_directories(temp_dir);
    std::ofstream{temp_dir / "module.ifo.json"} << "{}";

    constexpr std::string_view header = R"([GENERAL]
Name=temp_rules
Type=SET
Version=V1.0
Interior=0
HasHeightTransition=1
DisplayName=-1
Border=0
Default=0
Floor=0

[TERRAIN TYPES]
Count=2

[TERRAIN0]
Name=FloorA
StrRef=0

[TERRAIN1]
Name=FloorB
StrRef=0

[CROSSER TYPES]
Count=0

[TILES]
Count=1

[TILE0]
Model=tno01_r01_01
WalkMesh=msb01
TopLeft=FloorA
TopLeftHeight=0
TopRight=FloorA
TopRightHeight=0
BottomLeft=FloorA
BottomLeftHeight=0
BottomRight=FloorA
BottomRightHeight=0
Top=
Right=
Bottom=
Left=
MainLight1=1
MainLight2=1
SourceLight1=1
SourceLight2=1
AnimLoop1=1
AnimLoop2=1
AnimLoop3=1
Doors=0
Sounds=0
PathNode=A
Orientation=0
ImageMap2D=mit001_r01_01

[GROUPS]
Count=0
)";

    const auto write_variant = [&](std::string_view resref, std::string_view rules_section) {
        std::ofstream out{temp_dir / (std::string{resref} + ".set")};
        out << header << "\n"
            << rules_section;
    };

    write_variant("pr_zero", "[PRIMARY RULES]\nCount=0\n");
    write_variant("pr_neg", "[PRIMARY RULES]\nCount=-1\n");
    write_variant("pr_huge", "[PRIMARY RULES]\nCount=65537\n");
    write_variant("pr_missing", "[PRIMARY RULES]\nCount=1\n");
    write_variant("pr_unk_pl", "[PRIMARY RULES]\nCount=1\n[PRIMARY RULE0]\nPlaced=Missing\nPlacedHeight=0\nAdjacent=FloorB\nAdjacentHeight=0\nChanged=FloorA\nChangedHeight=0\n");
    write_variant("pr_unk_adj", "[PRIMARY RULES]\nCount=1\n[PRIMARY RULE0]\nPlaced=FloorA\nPlacedHeight=0\nAdjacent=Missing\nAdjacentHeight=0\nChanged=FloorA\nChangedHeight=0\n");
    write_variant("pr_unk_chg", "[PRIMARY RULES]\nCount=1\n[PRIMARY RULE0]\nPlaced=FloorA\nPlacedHeight=0\nAdjacent=FloorB\nAdjacentHeight=0\nChanged=Missing\nChangedHeight=0\n");
    write_variant("pr_bad_ph", "[PRIMARY RULES]\nCount=1\n[PRIMARY RULE0]\nPlaced=FloorA\nPlacedHeight=2\nAdjacent=FloorB\nAdjacentHeight=0\nChanged=FloorA\nChangedHeight=0\n");
    write_variant("pr_bad_ah", "[PRIMARY RULES]\nCount=1\n[PRIMARY RULE0]\nPlaced=FloorA\nPlacedHeight=0\nAdjacent=FloorB\nAdjacentHeight=-1\nChanged=FloorA\nChangedHeight=0\n");
    write_variant("pr_bad_ch", "[PRIMARY RULES]\nCount=1\n[PRIMARY RULE0]\nPlaced=FloorA\nPlacedHeight=0\nAdjacent=FloorB\nAdjacentHeight=0\nChanged=FloorA\nChangedHeight=2\n");
    write_variant("pr_dup_key",
        "[PRIMARY RULES]\nCount=2\n"
        "[PRIMARY RULE0]\nPlaced=FloorA\nPlacedHeight=0\nAdjacent=FloorB\nAdjacentHeight=0\nChanged=FloorA\nChangedHeight=0\n"
        "[PRIMARY RULE1]\nPlaced=floora\nPlacedHeight=0\nAdjacent=FLOORB\nAdjacentHeight=0\nChanged=FloorB\nChangedHeight=0\n");

    ASSERT_TRUE(nwk::resman().load_module(temp_dir));
    nw::String refresh_error;
    ASSERT_TRUE(nwk::resman().refresh_module_resources(refresh_error)) << refresh_error;

    auto* zero = nwk::tilesets().load("pr_zero");
    ASSERT_NE(zero, nullptr);
    EXPECT_TRUE(zero->primary_rules_valid);
    EXPECT_TRUE(zero->primary_rules.empty());

    for (const std::string_view bad_name : {
             "pr_neg",
             "pr_huge",
             "pr_missing",
             "pr_unk_pl",
             "pr_unk_adj",
             "pr_unk_chg",
             "pr_bad_ph",
             "pr_bad_ah",
             "pr_bad_ch",
             "pr_dup_key",
         }) {
        SCOPED_TRACE(bad_name);
        auto* set = nwk::tilesets().load(bad_name);
        ASSERT_NE(set, nullptr);
        EXPECT_EQ(set->tiles.size(), 1u);
        EXPECT_FALSE(set->primary_rules_valid);
        EXPECT_TRUE(set->primary_rules.empty());
    }

    nwk::resman().unload_module();
    fs::remove_all(temp_dir);
}
