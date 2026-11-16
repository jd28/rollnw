#include <gtest/gtest.h>

#include "../tools/client/area_door_hooks.hpp"
#include "../tools/client/area_tile_brush.hpp"
#include "../tools/client/area_tile_edits.hpp"
#include "../tools/client/area_tile_interaction.hpp"
#include "../tools/client/area_tile_palette.hpp"
#include "../tools/client/object_document.hpp"
#include "../tools/client/workspace.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nwk = nw::kernel;

namespace {

nw::AreaTile distinct_tile(
    int32_t id, int32_t height, int32_t orientation, uint8_t seed)
{
    return {
        .id = id,
        .height = height,
        .orientation = orientation,
        .animloop1 = static_cast<uint8_t>(seed + 0u),
        .animloop2 = static_cast<uint8_t>(seed + 1u),
        .animloop3 = static_cast<uint8_t>(seed + 2u),
        .mainlight1 = static_cast<uint8_t>(seed + 3u),
        .mainlight2 = static_cast<uint8_t>(seed + 4u),
        .srclight1 = static_cast<uint8_t>(seed + 5u),
        .srclight2 = static_cast<uint8_t>(seed + 6u),
    };
}

nw::Area* make_area(nw::Tileset& tileset, int32_t width, int32_t height)
{
    auto* area = nwk::objects().make<nw::Area>();
    if (!area) {
        return nullptr;
    }
    area->tileset = &tileset;
    area->tileset_resref = nw::Resref{"test_set"};
    area->width = width;
    area->height = height;
    return area;
}

void destroy_area(nw::Area* area)
{
    if (!area) {
        return;
    }
    area->clear();
    nwk::objects().destroy(area->handle());
}

nw::toolset::AreaTileEditBatch replace_tiles(nw::Area& area,
    std::span<const uint32_t> indices,
    int32_t tile_id,
    int32_t orientation)
{
    nw::toolset::AreaTileEditBatch result{
        .area = area.handle(),
    };
    result.rows.reserve(indices.size());
    for (const uint32_t tile_index : indices) {
        auto after = area.tiles[tile_index];
        after.id = tile_id;
        after.orientation = orientation;
        result.rows.push_back({
            .tile_index = tile_index,
            .before = area.tiles[tile_index],
            .after = after,
        });
    }
    return result;
}

} // namespace

TEST(ClientAreaTileEdits, PaintAndInversePreserveCompleteRows)
{
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.resize(3);
    auto* area = make_area(tileset, 2, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(0, 2, 1, 10),
        distinct_tile(1, -1, 2, 30),
    };
    const auto original = area->tiles;

    const std::array<uint32_t, 2> indices{0, 1};
    auto batch = replace_tiles(*area, indices, 2, 3);
    ASSERT_EQ(batch.rows.size(), 2u);

    const auto epoch_before = nw::toolset::object_mutation_state();
    const auto applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_EQ(applied.applied_count, 2u);
    EXPECT_EQ(area->tiles[0].id, 2);
    EXPECT_EQ(area->tiles[0].orientation, 3);
    EXPECT_EQ(area->tiles[0].height, original[0].height);
    EXPECT_EQ(area->tiles[0].animloop1, original[0].animloop1);
    EXPECT_EQ(area->tiles[0].srclight2, original[0].srclight2);
    EXPECT_EQ(area->tiles[1].height, original[1].height);
    EXPECT_EQ(area->tiles[1].mainlight2, original[1].mainlight2);

    const auto epoch_after = nw::toolset::object_mutation_state();
    EXPECT_EQ(epoch_after.epoch, epoch_before.epoch + 1u);
    EXPECT_EQ(epoch_after.area_structure_epoch,
        epoch_before.area_structure_epoch + 1u);
    EXPECT_EQ(epoch_after.area, area->handle());
    EXPECT_EQ(epoch_after.kind,
        nw::toolset::ObjectMutationKind::area_tiles);
    ASSERT_EQ(epoch_after.area_tile_indices.size(), 2u);
    EXPECT_EQ(epoch_after.area_tile_indices[0], 0u);
    EXPECT_EQ(epoch_after.area_tile_indices[1], 1u);

    const auto undone = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::inverse);
    ASSERT_TRUE(undone.ok()) << undone.diagnostic;
    ASSERT_EQ(area->tiles.size(), original.size());
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[index], original[index]));
    }
    const auto undo_mutation = nw::toolset::object_mutation_state();
    EXPECT_EQ(undo_mutation.kind,
        nw::toolset::ObjectMutationKind::area_tiles);
    ASSERT_EQ(undo_mutation.area_tile_indices.size(), 2u);
    EXPECT_EQ(undo_mutation.area_tile_indices[0], 0u);
    EXPECT_EQ(undo_mutation.area_tile_indices[1], 1u);

    destroy_area(area);
}

TEST(ClientAreaTileEdits, TileLightBatchUsesNwnRangesAndPreservesOtherFields)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    auto* area = make_area(tileset, 2, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(0, 0, 0, 1),
        distinct_tile(1, 2, 1, 20),
    };
    const auto original = area->tiles;
    const std::array<uint32_t, 2> indices{0, 1};

    nw::toolset::AreaTileEditBatch edit;
    auto built = nw::toolset::build_area_tile_light_edits(area->handle(),
        indices, nw::toolset::AreaTileLightSlot::main1, 31, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(edit.rows.size(), 2u);
    EXPECT_EQ(edit.rows[0].tile_index, 0u);
    EXPECT_EQ(edit.rows[1].tile_index, 1u);
    EXPECT_EQ(edit.rows[0].after.mainlight1, 31u);
    EXPECT_EQ(edit.rows[1].after.mainlight1, 31u);
    EXPECT_EQ(edit.rows[0].after.srclight2, original[0].srclight2);
    EXPECT_EQ(edit.rows[1].after.height, original[1].height);

    auto applied = nw::toolset::apply_area_tile_edits(
        edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_EQ(area->tiles[0].mainlight1, 31u);
    EXPECT_EQ(area->tiles[1].mainlight1, 31u);
    applied = nw::toolset::apply_area_tile_edits(
        edit, nw::toolset::ObjectEditDirection::inverse);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
        area->tiles[0], original[0]));
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
        area->tiles[1], original[1]));

    built = nw::toolset::build_area_tile_light_edits(area->handle(),
        indices, nw::toolset::AreaTileLightSlot::source2, 15, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    EXPECT_EQ(edit.rows[0].after.srclight2, 15u);
    EXPECT_EQ(edit.rows[1].after.srclight2, 15u);

    built = nw::toolset::build_area_tile_light_edits(area->handle(),
        indices, nw::toolset::AreaTileLightSlot::source1, 16, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());
    built = nw::toolset::build_area_tile_light_edits(area->handle(),
        indices, nw::toolset::AreaTileLightSlot::main2, 32, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());
    built = nw::toolset::build_area_tile_light_edits(area->handle(),
        indices, nw::toolset::AreaTileLightSlot::invalid, 0, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());

    const std::array<uint32_t, 2> duplicate_indices{0, 0};
    built = nw::toolset::build_area_tile_light_edits(area->handle(),
        duplicate_indices, nw::toolset::AreaTileLightSlot::main1, 1, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());
    const std::array<uint32_t, 2> descending_indices{1, 0};
    built = nw::toolset::build_area_tile_light_edits(area->handle(),
        descending_indices, nw::toolset::AreaTileLightSlot::main1, 1, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());

    const std::array<uint32_t, 1> first_index{0};
    built = nw::toolset::build_area_tile_light_edits(area->handle(),
        first_index, nw::toolset::AreaTileLightSlot::main1,
        original[0].mainlight1, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::empty);
    EXPECT_TRUE(edit.rows.empty());

    destroy_area(area);
}

TEST(ClientAreaTileEdits, InvalidBatchWritesNoRows)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    auto* area = make_area(tileset, 2, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(0, 0, 0, 1),
        distinct_tile(1, 0, 0, 20),
    };
    const auto original = area->tiles;

    nw::toolset::AreaTileEditBatch batch{
        .area = area->handle(),
        .rows = {
            {
                .tile_index = 0,
                .before = area->tiles[0],
                .after = distinct_tile(1, 0, 0, 1),
            },
            {
                .tile_index = 1,
                .before = area->tiles[1],
                .after = distinct_tile(99, 0, 0, 20),
            },
        },
    };
    const auto applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[0], original[0]));
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[1], original[1]));

    batch.rows[1].after = distinct_tile(1, 0, 0, 20);
    batch.rows[1].tile_index = 0;
    const auto duplicate = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(duplicate.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[0], original[0]));

    destroy_area(area);
}

TEST(ClientAreaTileEdits, StaleAndNoopBatchesWriteNothing)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    auto* area = make_area(tileset, 1, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {distinct_tile(0, 0, 0, 1)};

    nw::toolset::AreaTileEditBatch stale{
        .area = area->handle(),
        .rows = {{
            .tile_index = 0,
            .before = distinct_tile(1, 0, 0, 1),
            .after = distinct_tile(1, 0, 1, 1),
        }},
    };
    const auto epoch_before = nw::toolset::object_mutation_state();
    auto applied = nw::toolset::apply_area_tile_edits(
        stale, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::stale_value);
    EXPECT_EQ(area->tiles[0].id, 0);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch_before.epoch);

    nw::toolset::AreaTileEditBatch noop{
        .area = area->handle(),
        .rows = {{
            .tile_index = 0,
            .before = area->tiles[0],
            .after = area->tiles[0],
        }},
    };
    applied = nw::toolset::apply_area_tile_edits(
        noop, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::empty);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch_before.epoch);

    destroy_area(area);
}

TEST(ClientAreaTileEdits, RejectsMalformedBatchBoundariesAtomically)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    auto* area = make_area(tileset, 2, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(0, 0, 0, 1),
        distinct_tile(1, 0, 0, 20),
    };
    const auto original = area->tiles;

    nw::toolset::AreaTileEditBatch batch{
        .area = area->handle(),
        .rows = {
            {
                .tile_index = 1,
                .before = area->tiles[1],
                .after = distinct_tile(0, 0, 0, 20),
            },
            {
                .tile_index = 0,
                .before = area->tiles[0],
                .after = distinct_tile(1, 0, 0, 1),
            },
        },
    };
    auto applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);

    batch.rows.clear();
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::empty);

    batch = {
        .area = area->handle(),
        .rows = {{
            .tile_index = 0,
            .before = area->tiles[0],
            .after = distinct_tile(1, 0, 4, 1),
        }},
    };
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);

    batch.area = nw::ObjectHandle{};
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    batch.area = area->handle();

    area->width = 3;
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    area->width = 2;

    tileset.tile_height = std::numeric_limits<float>::infinity();
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    tileset.tile_height = 5.0f;

    batch = {
        .area = area->handle(),
        .rows = {{
            .tile_index = 2,
            .before = area->tiles[0],
            .after = distinct_tile(1, 0, 0, 1),
        }},
    };
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);

    batch.rows = {
        {
            .tile_index = 0,
            .before = area->tiles[0],
            .after = distinct_tile(1, 0, 0, 1),
        },
        {
            .tile_index = 1,
            .before = area->tiles[1],
            .after = distinct_tile(0, 0, 0, 20),
        },
        {
            .tile_index = 1,
            .before = area->tiles[1],
            .after = distinct_tile(0, 0, 0, 20),
        },
    };
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);

    batch.rows.resize(1);
    batch.rows[0] = {
        .tile_index = 0,
        .before = area->tiles[0],
        .after = distinct_tile(1, std::numeric_limits<int32_t>::max(), 0, 1),
    };
    tileset.tile_height = std::numeric_limits<float>::max();
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);

    tileset.tile_height = 0.0f;
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    tileset.tile_height = std::numeric_limits<float>::quiet_NaN();
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    tileset.tile_height = 5.0f;

    area->tileset = nullptr;
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    area->tileset = &tileset;

    area->width = 0;
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    area->width = std::numeric_limits<int16_t>::max() + 1;
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    area->width = 2;

    area->tiles[0].id = -1;
    batch.rows[0] = {
        .tile_index = 0,
        .before = area->tiles[0],
        .after = distinct_tile(1, 0, 0, 1),
    };
    applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    area->tiles[0] = original[0];

    ASSERT_EQ(area->tiles.size(), original.size());
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[index], original[index]));
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, OccupiedDoorHookRejectsTheCompleteBatch)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    tileset.tiles[0].door_slots.push_back({
        .position = {0.0f, 0.0f, 0.0f},
        .orientation = 0.0f,
        .type = 1,
    });
    auto* area = make_area(tileset, 1, 1);
    auto* door = nwk::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(door, nullptr);
    area->tiles = {distinct_tile(0, 0, 0, 1)};

    nw::toolset::AreaDoorHookSnapshot hooks;
    std::string diagnostic;
    ASSERT_TRUE(nw::toolset::build_area_door_hooks(*area, hooks, diagnostic))
        << diagnostic;
    ASSERT_EQ(hooks.hooks.size(), 1u);
    ASSERT_TRUE(nwk::objects().components().set_area(
        door->handle(), area->handle().id));
    ASSERT_TRUE(nwk::objects().components().set_position(
        door->handle(), hooks.hooks[0].position));
    area->doors.push_back(door);
    ASSERT_TRUE(nw::toolset::build_area_door_hooks(*area, hooks, diagnostic))
        << diagnostic;
    ASSERT_EQ(nw::toolset::area_door_hook_tile(hooks, door->handle()), 0u);
    EXPECT_FALSE(nw::toolset::area_door_hook_tile(
        hooks, area->handle()));
    auto ambiguous_hooks = hooks;
    ambiguous_hooks.width = 2;
    ambiguous_hooks.tile_offsets = {0, 1, 2};
    ambiguous_hooks.hooks.push_back({
        .occupant = door->handle(),
        .tile_index = 1,
    });
    EXPECT_FALSE(nw::toolset::area_door_hook_tile(
        ambiguous_hooks, door->handle()));

    const std::array<uint32_t, 1> indices{0};
    auto batch = replace_tiles(*area, indices, 1, 0);
    const auto applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_EQ(area->tiles[0].id, 0);

    destroy_area(area);
}

TEST(ClientAreaTileEdits, CandidateDoorHookRejectsAnIrreversibleEdit)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    tileset.tiles[1].door_slots.push_back({
        .position = {0.0f, 0.0f, 0.0f},
        .orientation = 0.0f,
        .type = 1,
    });
    auto* area = make_area(tileset, 1, 1);
    auto* door = nwk::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(door, nullptr);
    area->tiles = {distinct_tile(0, 0, 0, 1)};

    const std::array<nw::AreaTile, 1> candidate{
        distinct_tile(1, 0, 0, 1)};
    nw::toolset::AreaDoorHookSnapshot hooks;
    std::string diagnostic;
    ASSERT_TRUE(nw::toolset::build_area_door_hooks(
        *area, candidate, hooks, diagnostic))
        << diagnostic;
    ASSERT_EQ(hooks.hooks.size(), 1u);
    ASSERT_TRUE(nwk::objects().components().set_area(
        door->handle(), area->handle().id));
    ASSERT_TRUE(nwk::objects().components().set_position(
        door->handle(), hooks.hooks[0].position));
    area->doors.push_back(door);

    const std::array<uint32_t, 1> indices{0};
    auto batch = replace_tiles(*area, indices, 1, 0);
    const auto applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_EQ(area->tiles[0].id, 0);

    destroy_area(area);
}

TEST(ClientAreaTileEdits, UnoccupiedHookAndUnrelatedDoorAllowEdit)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    tileset.tiles[0].door_slots.push_back({
        .position = {0.0f, 0.0f, 0.0f},
        .orientation = 0.0f,
        .type = 1,
    });
    auto* area = make_area(tileset, 2, 1);
    auto* door = nwk::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(door, nullptr);
    area->tiles = {
        distinct_tile(0, 0, 0, 1),
        distinct_tile(1, 0, 0, 20),
    };
    ASSERT_TRUE(nwk::objects().components().set_area(
        door->handle(), area->handle().id));
    ASSERT_TRUE(nwk::objects().components().set_position(
        door->handle(), {15.0f, 5.0f, 0.0f}));
    area->doors.push_back(door);

    const std::array<uint32_t, 1> indices{0};
    auto batch = replace_tiles(*area, indices, 1, 0);
    const auto applied = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_EQ(area->tiles[0].id, 1);

    destroy_area(area);
}

TEST(ClientAreaTileEdits, CommitCreatesOneUndoableWorkspaceAction)
{
    nw::Tileset tileset;
    tileset.tiles.resize(1);
    nw::toolset::WorkspaceState workspace;
    auto* area = make_area(tileset, 1, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {distinct_tile(0, 0, 0, 1)};

    auto& tab = workspace.open_area_tab("areas/test.caf.json", "Test");
    ASSERT_TRUE(tab.document.adopt(area->handle()));
    nw::toolset::CommandContext context{
        .active_tab_id = tab.id,
        .area_object = area->handle(),
        .workspace = &workspace,
    };
    nw::toolset::AreaTileEditBatch noop{
        .area = area->handle(),
        .rows = {{
            .tile_index = 0,
            .before = area->tiles[0],
            .after = area->tiles[0],
        }},
    };
    const auto epoch_before_noop = nw::toolset::object_mutation_state().epoch;
    auto no_change = nw::toolset::commit_area_tile_edits(
        std::move(noop), "No change", context);
    EXPECT_EQ(no_change.status, nw::toolset::CommandStatus::noop);
    EXPECT_FALSE(no_change.undo_action);
    EXPECT_FALSE(tab.dirty);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch,
        epoch_before_noop);

    const std::array<uint32_t, 1> indices{0};
    auto batch = replace_tiles(*area, indices, 0, 1);

    auto committed = nw::toolset::commit_area_tile_edits(
        std::move(batch), "Rotate area tile", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_TRUE(tab.dirty);
    EXPECT_EQ(area->tiles[0].orientation, 1);

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(area->tiles[0].orientation, 0);
    auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(area->tiles[0].orientation, 1);
}

TEST(ClientAreaTileEdits, CellPickerUsesNearestPlaneAndStableEdgeTie)
{
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.resize(1);
    auto* area = make_area(tileset, 2, 2);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(0, 0, 0, 1),
        distinct_tile(0, 0, 0, 1),
        distinct_tile(0, 2, 0, 1),
        distinct_tile(0, 1, 0, 1),
    };

    auto hit = nw::toolset::pick_area_tile_cell(*area, {
                                                           .origin = {15.0f, 15.0f, 30.0f},
                                                           .direction = {0.0f, 0.0f, -2.0f},
                                                       });
    ASSERT_EQ(hit.status, nw::toolset::AreaTileCellPickStatus::hit);
    EXPECT_EQ(hit.tile_index, 3u);
    EXPECT_FLOAT_EQ(hit.position.z, 5.0f);
    EXPECT_FLOAT_EQ(hit.distance, 25.0f);

    hit = nw::toolset::pick_area_tile_cell(*area, {
                                                      .origin = {5.0f, 15.0f, 30.0f},
                                                      .direction = {0.0f, 0.0f, -1.0f},
                                                  });
    ASSERT_EQ(hit.status, nw::toolset::AreaTileCellPickStatus::hit);
    EXPECT_EQ(hit.tile_index, 2u);
    EXPECT_EQ(hit.position, (glm::vec3{5.0f, 15.0f, 10.0f}));
    EXPECT_FLOAT_EQ(hit.distance, 20.0f);

    hit = nw::toolset::pick_area_tile_cell(*area, {
                                                      .origin = {10.0f, 5.0f, 20.0f},
                                                      .direction = {0.0f, 0.0f, -1.0f},
                                                  });
    ASSERT_EQ(hit.status, nw::toolset::AreaTileCellPickStatus::hit);
    EXPECT_EQ(hit.tile_index, 0u);

    hit = nw::toolset::pick_area_tile_cell(*area, {
                                                      .origin = {-5.0f, -5.0f, 20.0f},
                                                      .direction = {0.0f, 0.0f, -1.0f},
                                                  });
    EXPECT_EQ(hit.status, nw::toolset::AreaTileCellPickStatus::miss);

    destroy_area(area);
}

TEST(ClientAreaTileEdits, CrosserPickerTargetsInteriorAndBoundaryEdges)
{
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.resize(1);
    auto* area = make_area(tileset, 2, 2);
    ASSERT_NE(area, nullptr);
    area->tiles.assign(4, distinct_tile(0, 0, 0, 1));

    const std::array hits{
        nw::toolset::AreaTileCellPick{
            .position = {0.25f, 5.0f, 0.0f},
            .tile_index = 0,
            .status = nw::toolset::AreaTileCellPickStatus::hit,
        },
        nw::toolset::AreaTileCellPick{
            .position = {9.75f, 5.0f, 0.0f},
            .tile_index = 0,
            .status = nw::toolset::AreaTileCellPickStatus::hit,
        },
        nw::toolset::AreaTileCellPick{
            .position = {15.0f, 19.75f, 0.0f},
            .tile_index = 3,
            .status = nw::toolset::AreaTileCellPickStatus::hit,
        },
        nw::toolset::AreaTileCellPick{
            .position = {25.0f, 15.0f, 0.0f},
            .tile_index = 3,
            .status = nw::toolset::AreaTileCellPickStatus::hit,
        },
    };
    std::array<nw::toolset::AreaTileCrosserEdge, hits.size()> edges{};
    nw::toolset::pick_area_tile_crosser_edges(*area, hits, edges);
    using Axis = nw::toolset::AreaTileEdgeAxis;
    EXPECT_EQ(edges[0], (nw::toolset::AreaTileCrosserEdge{.x = 0, .y = 0, .axis = Axis::vertical}));
    EXPECT_EQ(edges[1], (nw::toolset::AreaTileCrosserEdge{.x = 1, .y = 0, .axis = Axis::vertical}));
    EXPECT_EQ(edges[2], (nw::toolset::AreaTileCrosserEdge{.x = 1, .y = 2, .axis = Axis::horizontal}));
    EXPECT_EQ(edges[3], (nw::toolset::AreaTileCrosserEdge{.x = 2, .y = 1, .axis = Axis::vertical}));

    const nw::toolset::AreaTileCellPick overhang{
        .position = {18.0f, 5.0f, 0.0f},
        .tile_index = 0,
        .status = nw::toolset::AreaTileCellPickStatus::hit,
    };
    const auto overhang_target
        = nw::toolset::pick_area_tile_crosser_target(*area, overhang);
    EXPECT_EQ(overhang_target.tile_index, 1u);
    EXPECT_EQ(overhang_target.edge,
        (nw::toolset::AreaTileCrosserEdge{
            .x = 2,
            .y = 0,
            .axis = Axis::vertical,
        }));

    destroy_area(area);
}

TEST(ClientAreaTileEdits, CrosserPreviewSpansRunAcrossTargetEdges)
{
    using Axis = nw::toolset::AreaTileEdgeAxis;
    const std::array edges{
        nw::toolset::AreaTileCrosserEdge{
            .x = 1,
            .y = 1,
            .axis = Axis::horizontal,
        },
        nw::toolset::AreaTileCrosserEdge{
            .x = 1,
            .y = 0,
            .axis = Axis::horizontal,
        },
        nw::toolset::AreaTileCrosserEdge{
            .x = 1,
            .y = 1,
            .axis = Axis::vertical,
        },
        nw::toolset::AreaTileCrosserEdge{
            .x = 2,
            .y = 1,
            .axis = Axis::vertical,
        },
    };
    std::array<nw::toolset::AreaTileCrosserSpan, edges.size()> spans{};
    nw::toolset::resolve_area_tile_crosser_spans(2, 2, edges, spans);

    ASSERT_TRUE(spans[0].valid);
    EXPECT_EQ(spans[0].start, (glm::vec2{15.0f, 5.0f}));
    EXPECT_EQ(spans[0].end, (glm::vec2{15.0f, 15.0f}));
    ASSERT_TRUE(spans[1].valid);
    EXPECT_EQ(spans[1].start, (glm::vec2{15.0f, 0.0f}));
    EXPECT_EQ(spans[1].end, (glm::vec2{15.0f, 5.0f}));
    ASSERT_TRUE(spans[2].valid);
    EXPECT_EQ(spans[2].start, (glm::vec2{5.0f, 15.0f}));
    EXPECT_EQ(spans[2].end, (glm::vec2{15.0f, 15.0f}));
    ASSERT_TRUE(spans[3].valid);
    EXPECT_EQ(spans[3].start, (glm::vec2{15.0f, 15.0f}));
    EXPECT_EQ(spans[3].end, (glm::vec2{20.0f, 15.0f}));
}

TEST(ClientAreaTileEdits, GridLineIsContinuousAndCoalescesRepeatedCells)
{
    std::array<uint8_t, 16> visited{};
    std::vector<uint32_t> indices;
    indices.reserve(visited.size());

    auto result = nw::toolset::append_area_tile_grid_line(
        4, 4, {.x = 0, .y = 0}, {.x = 3, .y = 3}, visited, indices);
    ASSERT_EQ(result.status, nw::toolset::AreaTileLineStatus::success);
    EXPECT_EQ(indices, (std::vector<uint32_t>{0, 5, 10, 15}));
    EXPECT_EQ(result.appended_count, 4u);

    result = nw::toolset::append_area_tile_grid_line(
        4, 4, {.x = 3, .y = 3}, {.x = 0, .y = 3}, visited, indices);
    ASSERT_EQ(result.status, nw::toolset::AreaTileLineStatus::success);
    EXPECT_EQ(indices, (std::vector<uint32_t>{0, 5, 10, 15, 14, 13, 12}));
    EXPECT_EQ(result.appended_count, 3u);

    result = nw::toolset::append_area_tile_grid_line(
        4, 4, {.x = -1, .y = 0}, {.x = 0, .y = 0}, visited, indices);
    EXPECT_EQ(result.status, nw::toolset::AreaTileLineStatus::invalid_input);
    EXPECT_EQ(indices.size(), 7u);
}

TEST(ClientAreaTileEdits, CrosserLineAppendsCanonicalEdgesOnce)
{
    std::array<uint8_t, 24> visited{};
    std::vector<nw::toolset::AreaTileCrosserEdge> edges;
    using Axis = nw::toolset::AreaTileEdgeAxis;

    auto result = nw::toolset::append_area_tile_crosser_line(
        3, 3, {.x = 0, .y = 0}, {.x = 2, .y = 0}, visited, edges);
    ASSERT_EQ(result.status, nw::toolset::AreaTileLineStatus::success);
    EXPECT_EQ(edges,
        (std::vector<nw::toolset::AreaTileCrosserEdge>{
            {.x = 1, .y = 0, .axis = Axis::vertical},
            {.x = 2, .y = 0, .axis = Axis::vertical},
        }));

    result = nw::toolset::append_area_tile_crosser_line(
        3, 3, {.x = 2, .y = 0}, {.x = 2, .y = 2}, visited, edges);
    ASSERT_EQ(result.status, nw::toolset::AreaTileLineStatus::success);
    EXPECT_EQ(edges,
        (std::vector<nw::toolset::AreaTileCrosserEdge>{
            {.x = 1, .y = 0, .axis = Axis::vertical},
            {.x = 2, .y = 0, .axis = Axis::vertical},
            {.x = 2, .y = 1, .axis = Axis::horizontal},
            {.x = 2, .y = 2, .axis = Axis::horizontal},
        }));

    result = nw::toolset::append_area_tile_crosser_line(
        3, 3, {.x = 2, .y = 2}, {.x = 2, .y = 0}, visited, edges);
    EXPECT_EQ(result.status, nw::toolset::AreaTileLineStatus::success);
    EXPECT_EQ(result.appended_count, 0u);
    EXPECT_EQ(edges.size(), 4u);

    std::array<uint8_t, 24> diagonal_visited{};
    std::vector<nw::toolset::AreaTileCrosserEdge> diagonal_edges;
    result = nw::toolset::append_area_tile_crosser_line(
        3, 3, {.x = 0, .y = 0}, {.x = 2, .y = 2}, diagonal_visited,
        diagonal_edges);
    ASSERT_EQ(result.status, nw::toolset::AreaTileLineStatus::success);
    EXPECT_EQ(diagonal_edges,
        (std::vector<nw::toolset::AreaTileCrosserEdge>{
            {.x = 1, .y = 0, .axis = Axis::vertical},
            {.x = 1, .y = 1, .axis = Axis::horizontal},
            {.x = 2, .y = 1, .axis = Axis::vertical},
            {.x = 2, .y = 2, .axis = Axis::horizontal},
        }));
    result = nw::toolset::append_area_tile_crosser_line(
        3, 3, {.x = 2, .y = 2}, {.x = 0, .y = 0}, diagonal_visited,
        diagonal_edges);
    EXPECT_EQ(result.status, nw::toolset::AreaTileLineStatus::success);
    EXPECT_EQ(result.appended_count, 0u);
    EXPECT_EQ(diagonal_edges.size(), 4u);
}

TEST(ClientAreaTileEdits, PointerActionsUseOneModifierDecisionTable)
{
    using Action = nw::toolset::AreaTilePointerAction;
    using Button = nw::toolset::AreaTilePointerButton;
    using Modifier = nw::toolset::AreaTilePointerModifier;

    EXPECT_EQ(nw::toolset::resolve_area_tile_pointer_action({
                  .button = Button::primary,
                  .modifier = Modifier::none,
              }),
        Action::paint);
    EXPECT_EQ(nw::toolset::resolve_area_tile_pointer_action({
                  .button = Button::secondary,
                  .modifier = Modifier::none,
                  .secondary_paint_available = true,
              }),
        Action::paint);
    EXPECT_EQ(nw::toolset::resolve_area_tile_pointer_action({
                  .button = Button::secondary,
                  .modifier = Modifier::none,
              }),
        Action::none);
    EXPECT_EQ(nw::toolset::resolve_area_tile_pointer_action({
                  .button = Button::primary,
                  .modifier = Modifier::select,
              }),
        Action::select);
    EXPECT_EQ(nw::toolset::resolve_area_tile_pointer_action({
                  .button = Button::secondary,
                  .modifier = Modifier::select,
              }),
        Action::cycle_variation);
    EXPECT_EQ(nw::toolset::resolve_area_tile_pointer_action({
                  .button = Button::other,
                  .modifier = Modifier::select,
              }),
        Action::none);
    EXPECT_EQ(nw::toolset::resolve_area_tile_pointer_action({
                  .button = Button::primary,
                  .modifier = Modifier::blocked,
              }),
        Action::none);

    const std::array inputs{
        nw::toolset::AreaTilePointerInput{
            .button = Button::primary,
            .modifier = Modifier::select,
        },
        nw::toolset::AreaTilePointerInput{
            .button = Button::secondary,
            .modifier = Modifier::none,
        },
        nw::toolset::AreaTilePointerInput{
            .button = Button::primary,
            .modifier = Modifier::blocked,
        },
    };
    std::array<nw::toolset::AreaTilePointerResult, 3> results{};
    nw::toolset::resolve_area_tile_pointer_actions(inputs, results);
    EXPECT_EQ(results[0].action, Action::select);
    EXPECT_TRUE(results[0].consumed);
    EXPECT_EQ(results[1].action, Action::none);
    EXPECT_FALSE(results[1].consumed);
    EXPECT_EQ(results[2].action, Action::none);
    EXPECT_TRUE(results[2].consumed);

    const auto invalid = nw::toolset::resolve_area_tile_pointer_input({
        .button = Button::primary,
        .modifier = static_cast<Modifier>(UINT8_MAX),
    });
    EXPECT_EQ(invalid.action, Action::none);
    EXPECT_TRUE(invalid.consumed);

    std::array<nw::toolset::AreaTilePointerResult, 2> mismatched{{
        {.action = Action::paint, .consumed = true},
        {.action = Action::paint, .consumed = true},
    }};
    nw::toolset::resolve_area_tile_pointer_actions(inputs, mismatched);
    EXPECT_EQ(mismatched[0].action, Action::none);
    EXPECT_FALSE(mismatched[0].consumed);
    EXPECT_EQ(mismatched[1].action, Action::none);
    EXPECT_FALSE(mismatched[1].consumed);
}

TEST(ClientAreaTileEdits, HeightCursorTargetsNearestCornerAndIncidentCells)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 2, 2);
    ASSERT_NE(area, nullptr);
    area->tiles.assign(4, distinct_tile(109, 0, 0, 1));

    const nw::toolset::AreaTileCellPick hit{
        .position = {9.0f, 9.0f, 0.0f},
        .tile_index = 0,
        .status = nw::toolset::AreaTileCellPickStatus::hit,
    };
    const uint32_t corner
        = nw::toolset::pick_area_tile_corner(*area, hit);
    EXPECT_EQ(corner, 4u);
    const auto cells = nw::toolset::resolve_area_tile_corner_cell(
        area->width, area->height, corner);
    ASSERT_EQ(cells.count, 4u);
    EXPECT_EQ(cells.tile_indices,
        (std::array<uint32_t, 4>{0, 1, 2, 3}));

    const auto edge = nw::toolset::resolve_area_tile_corner_cell(
        area->width, area->height, 0);
    ASSERT_EQ(edge.count, 1u);
    EXPECT_EQ(edge.tile_indices[0], 0u);
    destroy_area(area);
}

TEST(ClientAreaTileEdits, TerrainBrushFitsIncidentTilesAndIsReversible)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 3, 3);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(9, distinct_tile(109, 0, 0, 1));
    const auto original = area->tiles;

    const std::array<uint32_t, 1> cells{4};
    nw::toolset::AreaTileEditBatch first;
    nw::toolset::AreaTileEditBatch second;
    const nw::toolset::AreaTileBrush brush{
        .kind = nw::toolset::AreaTileBrushKind::terrain,
        .value = 1,
    };
    auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells, brush, 42, first);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells, brush, 42, second);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(first.rows.size(), second.rows.size());
    for (size_t index = 0; index < first.rows.size(); ++index) {
        EXPECT_EQ(first.rows[index].tile_index, second.rows[index].tile_index);
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            first.rows[index].after, second.rows[index].after));
    }

    const auto applied = nw::toolset::apply_area_tile_edits(
        first, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    const auto undone = nw::toolset::apply_area_tile_edits(
        first, nw::toolset::ObjectEditDirection::inverse);
    ASSERT_TRUE(undone.ok()) << undone.diagnostic;
    ASSERT_EQ(area->tiles.size(), original.size());
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[index], original[index]));
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, PlacedTileVariationCyclesInSetOrder)
{
    nw::Tileset tileset;
    tileset.tiles.resize(3);
    tileset.terrains.resize(4);
    tileset.tile_topologies.resize(3);
    tileset.grouped_tiles = {0, 0, 0};
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain = {0, 1, 2, 3};
        topology.valid = true;
    }
    auto* area = make_area(tileset, 1, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {distinct_tile(0, 0, 0, 1)};
    const std::array<uint32_t, 1> cells{0};

    nw::toolset::AreaTileEditBatch edit;
    auto built = nw::toolset::build_area_tile_variation_edits(
        area->handle(), cells, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(edit.rows.size(), 1u);
    EXPECT_EQ(edit.rows[0].after.id, 1);
    auto applied = nw::toolset::apply_area_tile_edits(
        edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;

    edit = {};
    built = nw::toolset::build_area_tile_variation_edits(
        area->handle(), cells, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(edit.rows.size(), 1u);
    EXPECT_EQ(edit.rows[0].after.id, 2);
    applied = nw::toolset::apply_area_tile_edits(
        edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;

    edit = {};
    built = nw::toolset::build_area_tile_variation_edits(
        area->handle(), cells, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(edit.rows.size(), 1u);
    EXPECT_EQ(edit.rows[0].after.id, 0);
    destroy_area(area);
}

TEST(ClientAreaTileEdits, EraserChoosesCanonicalTileInsteadOfRandomVariation)
{
    nw::Tileset tileset;
    tileset.tiles.resize(3);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(3);
    tileset.grouped_tiles = {0, 0, 0};
    tileset.default_terrain = 0;
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain.fill(0);
        topology.valid = true;
    }
    auto* area = make_area(tileset, 1, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {distinct_tile(2, 0, 0, 1)};
    const std::array<uint32_t, 1> cells{0};

    nw::toolset::AreaTileEditBatch first;
    nw::toolset::AreaTileEditBatch second;
    const nw::toolset::AreaTileBrush eraser{
        .kind = nw::toolset::AreaTileBrushKind::eraser,
        .value = tileset.default_terrain,
    };
    auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells, eraser, 1, first);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells, eraser, 999, second);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(first.rows.size(), 1u);
    ASSERT_EQ(second.rows.size(), 1u);
    EXPECT_EQ(first.rows[0].after.id, 0);
    EXPECT_EQ(first.rows[0].after.orientation, 0);
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
        first.rows[0].after, second.rows[0].after));
    destroy_area(area);
}

TEST(ClientAreaTileEdits, VoidRowsCanBeMaterializedByTerrainBrush)
{
    nw::Tileset tileset;
    tileset.tile_height = 5.0f;
    tileset.tiles.resize(1);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(1);
    tileset.grouped_tiles = {0};
    tileset.tile_topologies[0].terrain.fill(0);
    tileset.tile_topologies[0].valid = true;
    auto* area = make_area(tileset, 2, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(0, 0, 0, 10),
        distinct_tile(0, 0, 0, 20),
    };

    const std::array<uint32_t, 1> cells{0};
    nw::toolset::AreaTileEraseEditBatch void_edit;
    auto built = nw::toolset::build_area_tile_void_edits(
        area->handle(), cells, void_edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(void_edit.tiles.rows.size(), 1u);
    const auto& void_row = void_edit.tiles.rows[0].after;
    EXPECT_TRUE(nw::area_tile_is_void(void_row));
    EXPECT_EQ(void_row.height, 0);
    EXPECT_EQ(void_row.orientation, 0);
    EXPECT_EQ(void_row.animloop1, 0);
    EXPECT_EQ(void_row.srclight2, 0);

    auto applied = nw::toolset::apply_area_tile_edits(
        void_edit.tiles, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_TRUE(nw::area_tile_is_void(area->tiles[0]));

    nw::toolset::AreaTileEditBatch terrain_edit;
    built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 0},
        17, terrain_edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_FALSE(terrain_edit.rows.empty());
    const auto restored = std::ranges::find_if(terrain_edit.rows,
        [](const auto& row) { return row.tile_index == 0; });
    ASSERT_NE(restored, terrain_edit.rows.end());
    EXPECT_EQ(restored->after.id, 0);

    applied = nw::toolset::apply_area_tile_edits(
        terrain_edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_FALSE(nw::area_tile_is_void(area->tiles[0]));
    destroy_area(area);
}

TEST(ClientAreaTileEdits, RaiseBrushBuildsTransitionsInsteadOfChangingOneRow)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 3, 3);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(9, distinct_tile(109, 0, 0, 1));

    const std::array<uint32_t, 1> corners{10};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_height_brush_edits(
        area->handle(), corners, 1, 7, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    EXPECT_GT(edit.rows.size(), 1u);
    EXPECT_LE(edit.rows.size(), 4u);
    const std::array<uint32_t, 4> incident{4, 5, 7, 8};
    EXPECT_TRUE(std::ranges::all_of(edit.rows,
        [&incident](const auto& row) {
            return std::ranges::find(incident, row.tile_index)
                != incident.end();
        }));
    EXPECT_NE(std::find_if(edit.rows.begin(), edit.rows.end(),
                  [](const auto& row) {
                      return row.after.height != row.before.height
                          || row.after.id != row.before.id;
                  }),
        edit.rows.end());
    destroy_area(area);
}

TEST(ClientAreaTileEdits, SmartBrushRejectsExistingInconsistentSeam)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 2, 1);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles = {
        distinct_tile(109, 0, 0, 1),
        distinct_tile(20, 0, 0, 20),
    };
    const auto original = area->tiles;

    const std::array<uint32_t, 1> cells{0};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::terrain,
            .value = 0,
        },
        1, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[index], original[index]));
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, GroupBrushPlacesTheSetGroupTile)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 3, 3);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(9, distinct_tile(109, 0, 0, 1));

    const std::array<uint32_t, 1> cells{4};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::group,
            .value = 0,
        },
        3, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    const auto center = std::find_if(edit.rows.begin(), edit.rows.end(),
        [](const auto& row) {
            return row.tile_index == 4;
        });
    ASSERT_NE(center, edit.rows.end());
    EXPECT_EQ(center->after.id, 118);
    destroy_area(area);
}

TEST(ClientAreaTileEdits, HeightBrushRejectsSplittingAPlacedGroup)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 3, 3);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(9, distinct_tile(109, 0, 0, 1));

    const std::array<uint32_t, 1> cells{4};
    nw::toolset::AreaTileEditBatch group_edit;
    auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::group,
            .value = 0,
        },
        3, group_edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    const auto applied = nw::toolset::apply_area_tile_edits(
        group_edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    ASSERT_EQ(area->tiles[4].id, 118);

    const std::array<uint32_t, 1> corners{10};
    nw::toolset::AreaTileEditBatch height_edit;
    built = nw::toolset::build_area_tile_height_brush_edits(
        area->handle(), corners, 1, 5, height_edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(height_edit.rows.empty());
    EXPECT_NE(built.diagnostic.find("split a placed tileset group"),
        std::string::npos);
    EXPECT_EQ(area->tiles[4].id, 118);

    destroy_area(area);
}

TEST(ClientAreaTileEdits, PlacedGroupSelectionAndEraserUseCompleteFootprint)
{
    nw::Tileset tileset;
    tileset.tiles.resize(3);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(3);
    tileset.grouped_tiles = {0, 1, 1};
    tileset.groups = {{
        .rows = 1,
        .columns = 2,
        .tile_offset = 0,
        .tile_count = 2,
    }};
    tileset.group_tile_ids = {1, 2};
    tileset.default_terrain = 0;
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain.fill(0);
        topology.valid = true;
    }
    auto* area = make_area(tileset, 2, 2);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(1, 0, 1, 10),
        distinct_tile(0, 0, 0, 1),
        distinct_tile(2, 0, 1, 20),
        distinct_tile(0, 0, 0, 2),
    };

    nw::toolset::AreaTileSelection selection;
    auto selected = nw::toolset::build_area_tile_selection(
        area->handle(), 2, selection);
    ASSERT_TRUE(selected.ok()) << selected.diagnostic;
    EXPECT_EQ(selection.source_tile_index, 2u);
    EXPECT_EQ(selection.group_index, 0u);
    EXPECT_EQ(selection.tile_indices, (std::vector<uint32_t>{0, 2}));

    selection = {};
    selected = nw::toolset::build_area_tile_selection(
        area->handle(), 1, selection);
    ASSERT_TRUE(selected.ok()) << selected.diagnostic;
    EXPECT_FALSE(selection.is_group());
    EXPECT_EQ(selection.tile_indices, (std::vector<uint32_t>{1}));

    const std::array<uint32_t, 1> cells{2};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::eraser,
            .value = tileset.default_terrain,
        },
        6, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(edit.rows.size(), 2u);
    EXPECT_EQ(edit.rows[0].tile_index, 0u);
    EXPECT_EQ(edit.rows[0].after.id, 0);
    EXPECT_EQ(edit.rows[1].tile_index, 2u);
    EXPECT_EQ(edit.rows[1].after.id, 0);

    nw::toolset::AreaTileEraseEditBatch void_edit;
    const auto voided = nw::toolset::build_area_tile_void_edits(
        area->handle(), cells, void_edit);
    ASSERT_TRUE(voided.ok()) << voided.diagnostic;
    ASSERT_EQ(void_edit.tiles.rows.size(), 2u);
    EXPECT_EQ(void_edit.tiles.rows[0].tile_index, 0u);
    EXPECT_TRUE(nw::area_tile_is_void(void_edit.tiles.rows[0].after));
    EXPECT_EQ(void_edit.tiles.rows[1].tile_index, 2u);
    EXPECT_TRUE(nw::area_tile_is_void(void_edit.tiles.rows[1].after));

    area->tiles[0].id = 0;
    edit = {};
    const auto incomplete = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::eraser,
            .value = tileset.default_terrain,
        },
        6, edit);
    EXPECT_EQ(incomplete.status,
        nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());
    EXPECT_NE(incomplete.diagnostic.find("incomplete"), std::string::npos);

    selection = {};
    const auto incomplete_selection
        = nw::toolset::build_area_tile_selection(
            area->handle(), 2, selection);
    EXPECT_EQ(incomplete_selection.status,
        nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_FALSE(selection.active());
    EXPECT_NE(incomplete_selection.diagnostic.find("incomplete"),
        std::string::npos);
    destroy_area(area);

    tileset.groups[0] = {
        .rows = 1,
        .columns = 3,
        .tile_offset = 0,
        .tile_count = 3,
    };
    tileset.group_tile_ids = {1, -1, 2};
    area = make_area(tileset, 3, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(1, 0, 0, 10),
        distinct_tile(0, 0, 0, 10),
        distinct_tile(2, 0, 0, 10),
    };
    selection = {};
    selected = nw::toolset::build_area_tile_selection(
        area->handle(), 1, selection);
    ASSERT_TRUE(selected.ok()) << selected.diagnostic;
    EXPECT_TRUE(selection.is_group());
    EXPECT_EQ(selection.tile_indices, (std::vector<uint32_t>{0, 1, 2}));

    edit = {};
    const std::array<uint32_t, 1> random_cell{1};
    std::vector<uint32_t> erase_preview_cells;
    auto erase_targets = nw::toolset::resolve_area_tile_erase_cells(
        area->handle(), random_cell, erase_preview_cells);
    ASSERT_TRUE(erase_targets.ok()) << erase_targets.diagnostic;
    EXPECT_EQ(erase_preview_cells, (std::vector<uint32_t>{0, 1, 2}));
    const auto erased_random_cell = nw::toolset::build_area_tile_brush_edits(
        area->handle(), random_cell,
        {
            .kind = nw::toolset::AreaTileBrushKind::eraser,
            .value = tileset.default_terrain,
        },
        6, edit);
    ASSERT_TRUE(erased_random_cell.ok()) << erased_random_cell.diagnostic;
    ASSERT_EQ(edit.rows.size(), 2u);
    EXPECT_EQ(edit.rows[0].tile_index, 0u);
    EXPECT_EQ(edit.rows[1].tile_index, 2u);
    EXPECT_EQ(edit.rows[0].after.id, 0);
    EXPECT_EQ(edit.rows[1].after.id, 0);

    const std::array<uint32_t, 1> invalid_cell{3};
    erase_targets = nw::toolset::resolve_area_tile_erase_cells(
        area->handle(), invalid_cell, erase_preview_cells);
    EXPECT_EQ(erase_targets.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(erase_preview_cells.empty());
    const std::array<uint32_t, 2> duplicates{1, 1};
    erase_targets = nw::toolset::resolve_area_tile_erase_cells(
        area->handle(), duplicates, erase_preview_cells);
    EXPECT_EQ(erase_targets.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(erase_preview_cells.empty());
    area->tiles[0].id = 0;
    const std::array<uint32_t, 1> incomplete_cell{2};
    erase_targets = nw::toolset::resolve_area_tile_erase_cells(
        area->handle(), incomplete_cell, erase_preview_cells);
    EXPECT_EQ(erase_targets.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(erase_preview_cells.empty());
    destroy_area(area);
}

TEST(ClientAreaTileEdits, EraserRemovesRotatedGroupAndDoorsAsOneUndoAction)
{
    nw::Tileset tileset;
    tileset.tiles.resize(3);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(3);
    tileset.grouped_tiles = {0, 1, 1};
    tileset.groups = {{.rows = 1, .columns = 2, .tile_offset = 0, .tile_count = 2}};
    tileset.group_tile_ids = {1, 2};
    tileset.default_terrain = 0;
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain.fill(0);
        topology.valid = true;
    }
    for (int32_t id : {1, 2}) {
        tileset.tiles[id].door_slots.push_back({.position = {0.0f, 0.0f, 0.0f}, .type = 1});
    }
    nw::toolset::WorkspaceState workspace;
    auto* area = make_area(tileset, 2, 2);
    ASSERT_NE(area, nullptr);
    area->tiles = {distinct_tile(1, 0, 1, 10), distinct_tile(0, 0, 0, 20),
        distinct_tile(2, 0, 1, 30), distinct_tile(0, 0, 0, 40)};
    const auto original = area->tiles;
    auto& tab = workspace.open_area_tab("areas/test.caf.json", "Test");
    ASSERT_TRUE(tab.document.adopt(area->handle()));
    nw::toolset::AreaDoorHookSnapshot hooks;
    std::string diagnostic;
    ASSERT_TRUE(nw::toolset::build_area_door_hooks(*area, hooks, diagnostic)) << diagnostic;
    ASSERT_EQ(hooks.hooks.size(), 2u);
    auto* first = nwk::objects().make<nw::Door>();
    auto* second = nwk::objects().make<nw::Door>();
    auto* unrelated = nwk::objects().make<nw::Door>();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(unrelated, nullptr);
    ASSERT_TRUE(nwk::objects().components().set_position(first->handle(), hooks.hooks[0].position));
    ASSERT_TRUE(nwk::objects().components().set_position(second->handle(), hooks.hooks[1].position));
    ASSERT_TRUE(nwk::objects().components().set_position(unrelated->handle(), {15.0f, 5.0f, 0.0f}));
    area->doors = {unrelated, second, first};
    const auto original_doors = area->doors;
    const auto first_handle = first->handle();
    const auto second_handle = second->handle();
    nw::toolset::CommandContext context{.active_tab_id = tab.id,
        .area_object = area->handle(),
        .workspace = &workspace};
    const std::array<uint32_t, 1> cells{2};
    nw::toolset::AreaTileEraseEditBatch edit;
    const auto built = nw::toolset::build_area_tile_erase_edits(
        area->handle(), cells, 0, 5, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(edit.tiles.rows.size(), 2u);
    ASSERT_EQ(edit.doors.size(), 2u);
    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto erased = nw::toolset::commit_area_tile_erase_edits(std::move(edit), "Erase group", context);
    ASSERT_TRUE(erased.ok()) << erased.message;
    ASSERT_TRUE(erased.undo_action);
    EXPECT_TRUE(tab.dirty);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind,
        nw::toolset::ObjectMutationKind::structure);
    EXPECT_TRUE(nw::toolset::object_mutation_state()
            .area_tile_indices.empty());
    EXPECT_EQ(area->doors, (nw::Vector<nw::Door*>{unrelated}));
    EXPECT_TRUE(nwk::objects().valid(first_handle));
    EXPECT_TRUE(nwk::objects().valid(second_handle));
    EXPECT_EQ(area->tiles[0].id, 0);
    EXPECT_EQ(area->tiles[2].id, 0);
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[1], original[1]));
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[3], original[3]));
    workspace.push_undo(*erased.undo_action);
    EXPECT_EQ(workspace.undo_count(), 1u);
    ASSERT_TRUE(workspace.undo(context).ok());
    EXPECT_EQ(area->doors, original_doors);
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[i], original[i]));
    }
    ASSERT_TRUE(workspace.redo(context).ok());
    EXPECT_EQ(area->doors, (nw::Vector<nw::Door*>{unrelated}));
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 3);
}

TEST(ClientAreaTileEdits, TileEraserOwnsDetachedDoorLifetime)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(2);
    tileset.grouped_tiles = {0, 0};
    tileset.default_terrain = 0;
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain.fill(0);
        topology.valid = true;
    }
    tileset.tiles[1].door_slots.push_back(
        {.position = {0.0f, 0.0f, 0.0f}, .type = 1});
    auto* area = make_area(tileset, 1, 1);
    auto* door = nwk::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(door, nullptr);
    area->tiles = {distinct_tile(1, 0, 0, 10)};
    nw::toolset::AreaDoorHookSnapshot hooks;
    std::string diagnostic;
    ASSERT_TRUE(nw::toolset::build_area_door_hooks(*area, hooks, diagnostic))
        << diagnostic;
    ASSERT_EQ(hooks.hooks.size(), 1u);
    ASSERT_TRUE(nwk::objects().components().set_position(
        door->handle(), hooks.hooks[0].position));
    area->doors = {door};
    const auto tile = area->tiles[0];
    const auto handle = door->handle();
    const std::array<uint32_t, 1> cells{0};
    nw::toolset::AreaTileEraseEditBatch edit;
    ASSERT_TRUE(nw::toolset::build_area_tile_erase_edits(
        area->handle(), cells, 0, 5, edit)
            .ok());
    ASSERT_EQ(edit.tiles.rows.size(), 1u);
    EXPECT_EQ(edit.tiles.rows[0].after.id, 0);
    EXPECT_EQ(edit.doors, (std::vector<nw::ObjectHandle>{handle}));
    nw::toolset::CommandContext context;
    auto erased = nw::toolset::commit_area_tile_erase_edits(
        std::move(edit), "Erase tile", context);
    ASSERT_TRUE(erased.ok()) << erased.message;
    ASSERT_TRUE(erased.undo_action);
    EXPECT_TRUE(area->doors.empty());
    EXPECT_TRUE(nwk::objects().valid(handle));
    ASSERT_TRUE(erased.undo_action->undo(context).ok());
    EXPECT_EQ(area->doors, (nw::Vector<nw::Door*>{door}));
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[0], tile));
    ASSERT_TRUE(erased.undo_action->redo(context).ok());
    EXPECT_EQ(area->tiles[0].id, 0);
    erased.undo_action.reset();
    EXPECT_FALSE(nwk::objects().valid(handle));
    destroy_area(area);
}

TEST(ClientAreaTileEdits, EraserRejectsStaleTilesAndDoorOnlyBatchesBeforeMutation)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    auto* area = make_area(tileset, 1, 1);
    auto* door = nwk::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(door, nullptr);
    area->tiles = {distinct_tile(0, 0, 0, 1)};
    area->doors = {door};
    const std::array<uint32_t, 1> cells{0};
    const auto handle = door->handle();
    nw::toolset::AreaTileEraseEditBatch edit{
        .tiles = replace_tiles(*area, cells, 1, 0), .doors = {handle}};
    edit.tiles.rows[0].before.id = 1;
    nw::toolset::CommandContext context;
    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto rejected = nw::toolset::commit_area_tile_erase_edits(std::move(edit), "Erase", context);
    EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
    EXPECT_FALSE(rejected.undo_action);
    EXPECT_EQ(area->doors, (nw::Vector<nw::Door*>{door}));
    EXPECT_EQ(area->tiles[0].id, 0);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch);
    nw::toolset::AreaTileEraseEditBatch door_only{
        .tiles = {.area = area->handle()}, .doors = {handle}};
    rejected = nw::toolset::commit_area_tile_erase_edits(
        std::move(door_only), "Erase", context);
    EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
    EXPECT_EQ(area->doors, (nw::Vector<nw::Door*>{door}));
    EXPECT_EQ(area->tiles[0].id, 0);
    auto built = nw::toolset::build_area_tile_erase_edits(
        area->handle(), {}, 0, 5, edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.tiles.rows.empty());
    EXPECT_TRUE(edit.doors.empty());
    destroy_area(area);
}

TEST(ClientAreaTileEdits, EraserPreservesUnselectedCandidateHookOccupants)
{
    nw::Tileset tileset;
    tileset.tiles.resize(2);
    tileset.tiles[0].door_slots.push_back({.position = {0.0f, 0.0f, 0.0f}, .type = 1});
    auto* area = make_area(tileset, 1, 1);
    auto* selected = nwk::objects().make<nw::Door>();
    auto* unrelated = nwk::objects().make<nw::Door>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(selected, nullptr);
    ASSERT_NE(unrelated, nullptr);
    area->tiles = {distinct_tile(1, 0, 0, 1)};
    nw::toolset::AreaDoorHookSnapshot hooks;
    std::string diagnostic;
    const std::array<nw::AreaTile, 1> candidate{distinct_tile(0, 0, 0, 1)};
    ASSERT_TRUE(nw::toolset::build_area_door_hooks(*area, candidate, hooks, diagnostic));
    ASSERT_EQ(hooks.hooks.size(), 1u);
    ASSERT_TRUE(nwk::objects().components().set_position(unrelated->handle(), hooks.hooks[0].position));
    ASSERT_TRUE(nwk::objects().components().set_position(selected->handle(), hooks.hooks[0].position));
    area->doors = {selected, unrelated};
    const std::array<uint32_t, 1> cells{0};
    nw::toolset::AreaTileEraseEditBatch edit{
        .tiles = replace_tiles(*area, cells, 0, 0), .doors = {selected->handle()}};
    nw::toolset::CommandContext context;
    const auto epoch = nw::toolset::object_mutation_state().epoch;
    const auto rejected = nw::toolset::commit_area_tile_erase_edits(std::move(edit), "Erase", context);
    EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
    EXPECT_EQ(area->doors, (nw::Vector<nw::Door*>{selected, unrelated}));
    EXPECT_EQ(area->tiles[0].id, 1);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch);
    destroy_area(area);
}

TEST(ClientAreaTileEdits, EraserFitsRealRandomGroupFootprintsInEveryOrientation)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 4, 4);
    ASSERT_NE(area, nullptr);
    for (int32_t orientation = 0; orientation < 4; ++orientation) {
        SCOPED_TRACE(orientation);
        area->tiles.assign(16, distinct_tile(20, 0, 0, 1));
        const std::array<uint32_t, 1> anchor{5};
        nw::toolset::AreaTileEditBatch placed;
        auto built = nw::toolset::build_area_tile_brush_edits(area->handle(), anchor,
            {.kind = nw::toolset::AreaTileBrushKind::group, .value = 41, .orientation = orientation},
            19, placed);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(placed, nw::toolset::ObjectEditDirection::forward).ok());
        const auto original = area->tiles;
        for (const uint32_t target : {5u, 6u, 9u, 10u}) {
            SCOPED_TRACE(target);
            const std::array cells{target};
            nw::toolset::AreaTileEraseEditBatch edit;
            built = nw::toolset::build_area_tile_erase_edits(
                area->handle(), cells, tileset->default_terrain, 5, edit);
            ASSERT_TRUE(built.ok()) << built.diagnostic;
            nw::toolset::CommandContext context;
            auto erased = nw::toolset::commit_area_tile_erase_edits(std::move(edit), "Erase", context);
            ASSERT_TRUE(erased.ok()) << erased.message;
            for (const uint32_t cell : {5u, 6u, 9u, 10u}) {
                EXPECT_EQ(tileset->grouped_tiles[area->tiles[cell].id], 0);
            }
            ASSERT_TRUE(erased.undo_action->undo(context).ok());
            for (size_t i = 0; i < original.size(); ++i) {
                EXPECT_TRUE(nw::toolset::area_tile_rows_equal(area->tiles[i], original[i]));
            }
        }
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, EraserNeverWidensAcrossLoadingAreaGroups)
{
    auto* tileset = nwk::tilesets().load("tdr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 4, 4);
    ASSERT_NE(area, nullptr);
    const std::array<int32_t, 16> ids{
        177,
        175,
        215,
        177,
        175,
        162,
        162,
        175,
        175,
        162,
        162,
        175,
        177,
        215,
        175,
        177,
    };
    const std::array<int32_t, 16> orientations{
        3,
        1,
        1,
        0,
        0,
        0,
        0,
        2,
        0,
        0,
        0,
        2,
        2,
        3,
        3,
        1,
    };
    area->tiles.resize(ids.size());
    for (size_t index = 0; index < ids.size(); ++index) {
        area->tiles[index].id = ids[index];
        area->tiles[index].orientation = orientations[index];
    }
    for (uint32_t tile_index = 0; tile_index < area->tiles.size();
        ++tile_index) {
        SCOPED_TRACE(tile_index);
        const std::array cells{tile_index};
        std::vector<uint32_t> targets;
        const auto resolved = nw::toolset::resolve_area_tile_erase_cells(
            area->handle(), cells, targets);
        ASSERT_TRUE(resolved.ok()) << resolved.diagnostic;
        EXPECT_EQ(targets, (std::vector<uint32_t>{tile_index}));
        nw::toolset::AreaTileEraseEditBatch erased;
        const auto built = nw::toolset::build_area_tile_erase_edits(
            area->handle(), cells, tileset->default_terrain, 5, erased);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        EXPECT_FALSE(erased.tiles.rows.empty());
        for (const uint32_t grouped_cell : {5u, 6u, 9u, 10u}) {
            if (grouped_cell == tile_index) {
                continue;
            }
            EXPECT_EQ(std::ranges::find_if(erased.tiles.rows,
                          [grouped_cell](const auto& row) {
                              return row.tile_index == grouped_cell;
                          }),
                erased.tiles.rows.end());
        }
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, EraserRemovesAllRealGroupsPlacedOnGrass)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 10, 10);
    ASSERT_NE(area, nullptr);
    size_t placed_groups = 0;
    for (size_t group = 0; group < tileset->groups.size(); ++group) {
        for (int32_t orientation = 0; orientation < 4; ++orientation) {
            SCOPED_TRACE(group);
            SCOPED_TRACE(orientation);
            area->tiles.assign(100, distinct_tile(20, 0, 0, 1));
            const std::array<uint32_t, 1> anchor{11};
            nw::toolset::AreaTileEditBatch placed;
            auto built = nw::toolset::build_area_tile_brush_edits(area->handle(), anchor,
                {.kind = nw::toolset::AreaTileBrushKind::group,
                    .value = static_cast<int32_t>(group),
                    .orientation = orientation},
                19, placed);
            // Groups requiring another terrain/height cannot be placed on this
            // grass fixture. Only successfully authored groups are erase inputs.
            if (!built.ok()) { continue; }
            ASSERT_TRUE(nw::toolset::apply_area_tile_edits(placed, nw::toolset::ObjectEditDirection::forward).ok());
            ++placed_groups;
            nw::toolset::AreaTileEraseEditBatch erased;
            built = nw::toolset::build_area_tile_erase_edits(area->handle(), anchor,
                tileset->default_terrain, 5, erased);
            EXPECT_TRUE(built.ok()) << built.diagnostic;
        }
    }
    EXPECT_GT(placed_groups, 0u);
    destroy_area(area);
}

TEST(ClientAreaTileEdits, GroupOverlapReplacesCompleteRotatedFootprintAndIsReversible)
{
    nw::Tileset tileset;
    tileset.tiles.resize(5);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(5);
    tileset.grouped_tiles = {0, 1, 1, 1, 1};
    tileset.groups = {
        {.rows = 1, .columns = 2, .tile_offset = 0, .tile_count = 2},
        {.rows = 1, .columns = 2, .tile_offset = 2, .tile_count = 2},
    };
    tileset.group_tile_ids = {1, 2, 3, 4};
    tileset.default_terrain = 0;
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain.fill(0);
        topology.valid = true;
    }
    for (int32_t orientation = 0; orientation < 4; ++orientation) {
        SCOPED_TRACE(orientation);
        auto* area = make_area(tileset, 5, 5);
        ASSERT_NE(area, nullptr);
        area->tiles.assign(25, distinct_tile(0, 0, 0, 10));

        const std::array<uint32_t, 1> old_anchor{6};
        nw::toolset::AreaTileEditBatch old_group;
        auto built = nw::toolset::build_area_tile_brush_edits(
            area->handle(), old_anchor,
            {.kind = nw::toolset::AreaTileBrushKind::group,
                .value = 0,
                .orientation = orientation},
            1, old_group);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            old_group, nw::toolset::ObjectEditDirection::forward)
                .ok());
        const auto original = area->tiles;
        const auto epoch = nw::toolset::object_mutation_state().epoch;

        const std::array<uint32_t, 1> new_anchor{
            orientation % 2 == 0 ? 7u : 11u};
        nw::toolset::AreaTileEditBatch replacement;
        built = nw::toolset::build_area_tile_brush_edits(
            area->handle(), new_anchor,
            {.kind = nw::toolset::AreaTileBrushKind::group,
                .value = 1,
                .orientation = orientation},
            2, replacement);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        ASSERT_EQ(replacement.rows.size(), 3u);
        EXPECT_EQ(replacement.rows[0].tile_index, 6u);
        EXPECT_EQ(replacement.rows[0].after.id, 0);
        EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch);
        for (size_t index = 0; index < original.size(); ++index) {
            EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
                area->tiles[index], original[index]));
        }

        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            replacement, nw::toolset::ObjectEditDirection::forward)
                .ok());
        EXPECT_TRUE(std::ranges::none_of(area->tiles,
            [](const auto& tile) { return tile.id == 1 || tile.id == 2; }));
        nw::toolset::AreaTileSelection selection;
        built = nw::toolset::build_area_tile_selection(
            area->handle(), new_anchor.front(), selection);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        EXPECT_EQ(selection.group_index, 1u);
        EXPECT_EQ(selection.tile_indices.size(), 2u);

        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            replacement, nw::toolset::ObjectEditDirection::inverse)
                .ok());
        for (size_t index = 0; index < original.size(); ++index) {
            EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
                area->tiles[index], original[index]));
        }
        area->tiles[6].id = 0;
        replacement = {};
        built = nw::toolset::build_area_tile_brush_edits(
            area->handle(), new_anchor,
            {.kind = nw::toolset::AreaTileBrushKind::group,
                .value = 1,
                .orientation = orientation},
            2, replacement);
        EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
        EXPECT_NE(built.diagnostic.find("incomplete"), std::string::npos);
        EXPECT_TRUE(replacement.rows.empty());
        destroy_area(area);
    }
}

TEST(ClientAreaTileEdits, GroupOverlapThroughRandomCellRemovesTheCompleteOldGroup)
{
    nw::Tileset tileset;
    tileset.tiles.resize(4);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(4);
    tileset.grouped_tiles = {0, 1, 1, 1};
    tileset.groups = {
        {.rows = 1, .columns = 3, .tile_offset = 0, .tile_count = 3},
        {.rows = 1, .columns = 1, .tile_offset = 3, .tile_count = 1},
    };
    tileset.group_tile_ids = {1, -1, 2, 3};
    tileset.default_terrain = 0;
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain.fill(0);
        topology.valid = true;
    }
    auto* area = make_area(tileset, 3, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(1, 0, 0, 10),
        distinct_tile(0, 0, 0, 20),
        distinct_tile(2, 0, 0, 30),
    };
    const std::array<uint32_t, 1> anchor{1};
    nw::toolset::AreaTileEditBatch replacement;
    const auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), anchor,
        {.kind = nw::toolset::AreaTileBrushKind::group, .value = 1},
        2, replacement);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_EQ(replacement.rows.size(), 3u);
    EXPECT_EQ(replacement.rows[0].after.id, 0);
    EXPECT_EQ(replacement.rows[1].after.id, 3);
    EXPECT_EQ(replacement.rows[2].after.id, 0);
    destroy_area(area);
}

TEST(ClientAreaTileEdits, GroupOverlapReplacesMultipleGroupsAndRejectsAmbiguity)
{
    nw::Tileset tileset;
    tileset.tiles.resize(5);
    tileset.terrains.resize(1);
    tileset.tile_topologies.resize(5);
    tileset.grouped_tiles = {0, 1, 1, 1, 1};
    tileset.groups = {
        {.rows = 1, .columns = 2, .tile_offset = 0, .tile_count = 2},
        {.rows = 1, .columns = 3, .tile_offset = 2, .tile_count = 3},
    };
    tileset.group_tile_ids = {1, 2, 3, -1, 4};
    tileset.default_terrain = 0;
    for (auto& topology : tileset.tile_topologies) {
        topology.terrain.fill(0);
        topology.valid = true;
    }
    auto* area = make_area(tileset, 5, 1);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(1, 0, 0, 10),
        distinct_tile(2, 0, 0, 20),
        distinct_tile(0, 0, 0, 30),
        distinct_tile(1, 0, 0, 40),
        distinct_tile(2, 0, 0, 50),
    };
    const std::array<uint32_t, 1> anchor{1};
    nw::toolset::AreaTileEditBatch replacement;
    auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), anchor,
        {.kind = nw::toolset::AreaTileBrushKind::group, .value = 1},
        2, replacement);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
        replacement, nw::toolset::ObjectEditDirection::forward)
            .ok());
    EXPECT_EQ(area->tiles[0].id, 0);
    EXPECT_EQ(area->tiles[1].id, 3);
    EXPECT_EQ(area->tiles[2].id, 0);
    EXPECT_EQ(area->tiles[3].id, 4);
    EXPECT_EQ(area->tiles[4].id, 0);

    tileset.groups = {
        {.rows = 1, .columns = 2, .tile_offset = 0, .tile_count = 2},
        {.rows = 1, .columns = 2, .tile_offset = 2, .tile_count = 2},
        {.rows = 1, .columns = 1, .tile_offset = 4, .tile_count = 1},
    };
    tileset.group_tile_ids = {1, 2, 2, 3, 4};
    area->tiles = {
        distinct_tile(1, 0, 0, 10),
        distinct_tile(2, 0, 0, 20),
        distinct_tile(3, 0, 0, 30),
        distinct_tile(0, 0, 0, 40),
        distinct_tile(0, 0, 0, 50),
    };
    const auto original = area->tiles;
    replacement = {};
    built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), anchor,
        {.kind = nw::toolset::AreaTileBrushKind::group, .value = 2},
        2, replacement);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_NE(built.diagnostic.find("ambiguous"), std::string::npos);
    EXPECT_TRUE(replacement.rows.empty());
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[index], original[index]));
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, CrosserBrushConnectsVisitedCells)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 3, 3);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(9, distinct_tile(109, 0, 0, 1));

    const std::array<uint32_t, 3> cells{3, 4, 5};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::crosser,
            .value = 3,
        },
        11, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    EXPECT_FALSE(edit.rows.empty());
    const auto applied = nw::toolset::apply_area_tile_edits(
        edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    destroy_area(area);
}

TEST(ClientAreaTileEdits, CrosserBrushPlacesOneBoundaryEdge)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 3, 3);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(9, distinct_tile(109, 0, 0, 1));

    using Axis = nw::toolset::AreaTileEdgeAxis;
    const std::array<uint32_t, 1> cells{1};
    const std::array edges{
        nw::toolset::AreaTileCrosserEdge{
            .x = 1,
            .y = 0,
            .axis = Axis::horizontal,
        },
    };
    nw::toolset::AreaTileEditBatch edit;
    auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::crosser,
            .value = 3,
        },
        11, edit, edges);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_FALSE(edit.rows.empty());
    EXPECT_NE(std::ranges::find(edit.rows, 1u,
                  &nw::toolset::AreaTileEditRow::tile_index),
        edit.rows.end());
    const auto applied = nw::toolset::apply_area_tile_edits(
        edit, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;

    const auto& tile = area->tiles[1];
    ASSERT_GE(tile.id, 0);
    ASSERT_LT(static_cast<size_t>(tile.id), tileset->tile_topologies.size());
    constexpr std::array<std::array<uint8_t, 4>, 4> world_to_local{{
        {0, 1, 2, 3},
        {1, 2, 3, 0},
        {2, 3, 0, 1},
        {3, 0, 1, 2},
    }};
    ASSERT_GE(tile.orientation, 0);
    ASSERT_LT(tile.orientation, 4);
    EXPECT_EQ(tileset->tile_topologies[static_cast<size_t>(tile.id)]
                  .crosser[world_to_local[static_cast<size_t>(tile.orientation)][2]],
        3);

    const std::array invalid_edges{
        nw::toolset::AreaTileCrosserEdge{
            .x = 4,
            .y = 0,
            .axis = Axis::vertical,
        },
    };
    edit = {};
    built = nw::toolset::build_area_tile_brush_edits(area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::crosser,
            .value = 3,
        },
        11, edit, invalid_edges);
    EXPECT_EQ(built.status,
        nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());

    const std::array duplicate_edges{edges[0], edges[0]};
    built = nw::toolset::build_area_tile_brush_edits(area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::crosser,
            .value = 3,
        },
        11, edit, duplicate_edges);
    EXPECT_EQ(built.status,
        nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(edit.rows.empty());
    destroy_area(area);
}

TEST(ClientAreaTileEdits, GroupBrushFitsRandomGroupCells)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 4, 4);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(16, distinct_tile(20, 0, 0, 1));

    const std::array<uint32_t, 1> cells{5};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::group,
            .value = 41,
        },
        19, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    const std::array<uint32_t, 3> fixed_indices{5, 6, 10};
    const std::array<int32_t, 3> fixed_ids{230, 180, 179};
    for (size_t index = 0; index < fixed_indices.size(); ++index) {
        const auto row = std::find_if(edit.rows.begin(), edit.rows.end(),
            [tile_index = fixed_indices[index]](const auto& value) {
                return value.tile_index == tile_index;
            });
        ASSERT_NE(row, edit.rows.end());
        EXPECT_EQ(row->after.id, fixed_ids[index]);
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, GroupBrushRotatesEveryGroupCell)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 4, 4);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles.assign(16, distinct_tile(20, 0, 0, 1));

    const std::array<uint32_t, 1> cells{5};
    nw::toolset::AreaTileEditBatch edit;
    const auto built = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::group,
            .value = 41,
            .orientation = 1,
        },
        19, edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    const std::array<uint32_t, 3> fixed_indices{6, 9, 10};
    const std::array<int32_t, 3> fixed_ids{230, 179, 180};
    for (size_t index = 0; index < fixed_indices.size(); ++index) {
        const auto row = std::find_if(edit.rows.begin(), edit.rows.end(),
            [tile_index = fixed_indices[index]](const auto& value) {
                return value.tile_index == tile_index;
            });
        ASSERT_NE(row, edit.rows.end());
        EXPECT_EQ(row->after.id, fixed_ids[index]);
        EXPECT_EQ(row->after.orientation, 1);
    }

    nw::toolset::AreaTileEditBatch invalid;
    const auto rejected = nw::toolset::build_area_tile_brush_edits(
        area->handle(), cells,
        {
            .kind = nw::toolset::AreaTileBrushKind::group,
            .value = 41,
            .orientation = 4,
        },
        19, invalid);
    EXPECT_EQ(rejected.status,
        nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_TRUE(invalid.rows.empty());
    destroy_area(area);
}

TEST(ClientAreaTileEdits, PaletteUsesTilesetActionsAndStableKeys)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 1, 1);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    area->tiles = {distinct_tile(0, 0, 0, 1)};

    nw::toolset::AreaTilePalette palette;
    ASSERT_TRUE(nw::toolset::build_area_tile_palette(
        area->handle(), palette));
    ASSERT_GT(palette.rows.size(), 10u);
    for (size_t index = 0; index < palette.rows.size(); ++index) {
        const auto& row = palette.rows[index];
        ASSERT_LE(static_cast<uint64_t>(row.child_offset) + row.child_count,
            palette.child_rows.size());
        if (row.kind == nw::toolset::AreaTilePaletteRowKind::action) {
            EXPECT_EQ(row.child_count, 0u);
            continue;
        }
        EXPECT_GT(row.subtree_end, index);
        EXPECT_LE(row.subtree_end, palette.rows.size());
        for (uint32_t child = 0; child < row.child_count; ++child) {
            const uint32_t child_index
                = palette.child_rows[row.child_offset + child];
            ASSERT_LT(child_index, palette.rows.size());
            EXPECT_EQ(palette.rows[child_index].parent, index);
        }
    }
    ASSERT_LT(palette.root_folder, palette.rows.size());
    EXPECT_EQ(palette.current_folder, palette.root_folder);
    ASSERT_EQ(palette.matches.size(), 4u);
    std::vector<std::string> root_labels;
    for (const uint32_t row_index : palette.matches) {
        root_labels.push_back(palette.rows[row_index].label);
    }
    EXPECT_EQ(root_labels,
        (std::vector<std::string>{"Features", "Groups", "Terrain",
            "Void Tiles"}));
    for (const uint32_t row_index : palette.matches) {
        ASSERT_LT(row_index, palette.rows.size());
        EXPECT_TRUE(palette.rows[row_index].kind
                == nw::toolset::AreaTilePaletteRowKind::folder
            || palette.rows[row_index].brush.kind
                == nw::toolset::AreaTileBrushKind::void_tile);
        EXPECT_EQ(palette.rows[row_index].parent, palette.root_folder);
    }
    for (const auto& folder : palette.rows) {
        if (folder.kind != nw::toolset::AreaTilePaletteRowKind::folder) {
            continue;
        }
        std::vector<std::string_view> labels;
        labels.reserve(folder.child_count);
        for (uint32_t child = 0; child < folder.child_count; ++child) {
            labels.push_back(palette.rows[palette.child_rows[folder.child_offset + child]].label);
        }
        EXPECT_TRUE(std::ranges::is_sorted(labels)) << folder.label;
    }
    const auto grass = std::find_if(palette.rows.begin(), palette.rows.end(),
        [](const auto& row) {
            return row.resref == "grass";
        });
    ASSERT_NE(grass, palette.rows.end());
    EXPECT_EQ(grass->brush.kind,
        nw::toolset::AreaTileBrushKind::terrain);
    EXPECT_GE(grass->brush.value, 0);
    EXPECT_FALSE(grass->category.empty());

    const auto terrain_folder = std::find_if(
        palette.rows.begin(), palette.rows.end(), [](const auto& row) {
            return row.kind == nw::toolset::AreaTilePaletteRowKind::folder
                && row.label == "Terrain";
        });
    ASSERT_NE(terrain_folder, palette.rows.end());
    const auto terrain_folder_index
        = static_cast<uint32_t>(terrain_folder - palette.rows.begin());
    ASSERT_TRUE(nw::toolset::enter_area_tile_palette_folder(
        palette, terrain_folder_index));
    EXPECT_EQ(palette.current_folder, terrain_folder_index);
    std::vector<std::string> terrain_labels;
    for (const uint32_t row_index : palette.matches) {
        terrain_labels.push_back(palette.rows[row_index].label);
    }
    EXPECT_EQ(terrain_labels,
        (std::vector<std::string>{"Eraser", "Grass", "Raise/Lower",
            "Road", "Stream", "Trees", "Wall 1", "Wall 2", "Water"}));
    EXPECT_NE(std::find(palette.matches.begin(), palette.matches.end(),
                  static_cast<uint32_t>(grass - palette.rows.begin())),
        palette.matches.end());
    EXPECT_TRUE(nw::toolset::leave_area_tile_palette_folder(palette));
    EXPECT_EQ(palette.current_folder, palette.root_folder);
    EXPECT_FALSE(nw::toolset::leave_area_tile_palette_folder(palette));

    for (size_t index = 0; index < tileset->terrains.size(); ++index) {
        const auto count = std::count_if(palette.rows.begin(), palette.rows.end(),
            [index](const auto& row) {
                return row.brush.kind
                    == nw::toolset::AreaTileBrushKind::terrain
                    && row.brush.value == static_cast<int32_t>(index)
                    && row.resref != "eraser";
            });
        EXPECT_EQ(count, 1) << tileset->terrains[index].name;
    }
    for (size_t index = 0; index < tileset->crossers.size(); ++index) {
        const auto count = std::count_if(palette.rows.begin(), palette.rows.end(),
            [index](const auto& row) {
                return row.brush.kind
                    == nw::toolset::AreaTileBrushKind::crosser
                    && row.brush.value == static_cast<int32_t>(index);
            });
        EXPECT_EQ(count, 1) << tileset->crossers[index].name;
    }

    const auto raise = std::find_if(palette.rows.begin(), palette.rows.end(),
        [](const auto& row) {
            return row.brush.kind
                == nw::toolset::AreaTileBrushKind::raise;
        });
    const auto lower = std::find_if(palette.rows.begin(), palette.rows.end(),
        [](const auto& row) {
            return row.brush.kind
                == nw::toolset::AreaTileBrushKind::lower;
        });
    const auto eraser = std::find_if(palette.rows.begin(), palette.rows.end(),
        [](const auto& row) {
            return row.brush.kind
                == nw::toolset::AreaTileBrushKind::eraser;
        });
    const auto void_tiles = std::find_if(
        palette.rows.begin(), palette.rows.end(), [](const auto& row) {
            return row.brush.kind
                == nw::toolset::AreaTileBrushKind::void_tile;
        });
    EXPECT_NE(raise, palette.rows.end());
    EXPECT_EQ(lower, palette.rows.end());
    EXPECT_NE(eraser, palette.rows.end());
    EXPECT_NE(void_tiles, palette.rows.end());
    EXPECT_EQ(void_tiles->label, "Void Tiles");
    EXPECT_EQ(raise->label, "Raise/Lower");

    ASSERT_TRUE(nw::toolset::filter_area_tile_palette(palette, "GRASS"));
    ASSERT_FALSE(palette.matches.empty());
    EXPECT_NE(std::find(palette.matches.begin(), palette.matches.end(),
                  static_cast<uint32_t>(grass - palette.rows.begin())),
        palette.matches.end());
    destroy_area(area);
}

TEST(ClientAreaTileEdits, PaletteDecodesOnlyTheRequestedThumbnail)
{
    nw::toolset::AreaTilePalette palette{
        .resource_generation = nwk::resman().generation(),
        .rows = {{
            .kind = nw::toolset::AreaTilePaletteRowKind::action,
            .image_map_2d = "qfpp_001_l",
            .thumbnail_source = "rollnw-area-tile://test/0",
        }},
        .matches = {0},
        .status = nw::toolset::AreaTilePaletteStatus::ready,
    };
    constexpr int row_index = 0;
    ASSERT_TRUE(nw::toolset::load_area_tile_palette_thumbnails(
        palette, row_index, row_index + 1));
    ASSERT_EQ(palette.textures.size(), 1u);
    const auto& texture = palette.textures.front();
    EXPECT_EQ(texture.source, palette.rows[0].thumbnail_source);
    EXPECT_GT(texture.width, 0u);
    EXPECT_GT(texture.height, 0u);
    EXPECT_EQ(texture.rgba.size(),
        static_cast<size_t>(texture.width) * texture.height * 4u);

    const auto source = texture.source;
    EXPECT_FALSE(nw::toolset::load_area_tile_palette_thumbnails(
        palette, -1, row_index + 1));
    ASSERT_EQ(palette.textures.size(), 1u);
    EXPECT_EQ(palette.textures.front().source, source);
    ASSERT_TRUE(nw::toolset::load_area_tile_palette_thumbnails(
        palette, 0, 0));
    EXPECT_TRUE(palette.textures.empty());
}

TEST(ClientAreaTileEdits, SaveAndReloadPreserveEditedTileRows)
{
    const std::filesystem::path project
        = "tmp/client_area_tile_save_reload";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    {
        std::ofstream target{project / "test_area.caf.json"};
        ASSERT_TRUE(target);
        target << "{}\n";
    }

    nw::toolset::WorkspaceState workspace;
    nw::Tileset tileset;
    tileset.tiles.resize(3);
    auto* area = make_area(tileset, 2, 2);
    ASSERT_NE(area, nullptr);
    area->tiles = {
        distinct_tile(0, 0, 0, 1),
        distinct_tile(1, 1, 1, 20),
        distinct_tile(2, 2, 2, 40),
        distinct_tile(0, 3, 3, 60),
    };
    const auto original = area->tiles;
    constexpr int32_t replacement_id = 2;
    constexpr int32_t replacement_orientation = 3;
    auto& tab = workspace.open_area_tab(
        "test_area.caf.json", "Test Area");
    ASSERT_TRUE(tab.document.adopt(area->handle()));
    nw::toolset::CommandContext context{
        .active_tab_id = tab.id,
        .area_object = area->handle(),
        .workspace = &workspace,
    };
    const std::array<uint32_t, 1> indices{0};
    auto batch = replace_tiles(*area, indices,
        replacement_id, replacement_orientation);
    auto edited = nw::toolset::commit_area_tile_edits(
        std::move(batch), "Paint area tile", context);
    ASSERT_TRUE(edited.ok()) << edited.message;
    workspace.push_undo(*edited.undo_action);
    const std::array<std::string_view, 1> ids{tab.id};
    const auto saved
        = nw::toolset::save_workspace_documents(workspace, project, ids);
    ASSERT_TRUE(saved.ok()) << saved.message;
    auto* reloaded = nwk::objects().make<nw::Area>();
    ASSERT_NE(reloaded, nullptr);
    {
        std::ifstream input{project / "test_area.caf.json"};
        ASSERT_TRUE(input);
        const auto archive = nlohmann::json::parse(input);
        ASSERT_TRUE(nw::deserialize(reloaded, archive));
    }
    ASSERT_FALSE(reloaded->tiles.empty());
    auto expected_first = original.front();
    expected_first.id = replacement_id;
    expected_first.orientation = replacement_orientation;
    EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
        reloaded->tiles.front(), expected_first));
    ASSERT_EQ(reloaded->tiles.size(), original.size());
    for (size_t index = 1; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            reloaded->tiles[index], original[index]));
    }
    reloaded->clear();
    nwk::objects().destroy(reloaded->handle());

    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[index], original[index]));
    }
    const auto restored
        = nw::toolset::save_workspace_documents(workspace, project, ids);
    ASSERT_TRUE(restored.ok()) << restored.message;
    auto* restored_reload = nwk::objects().make<nw::Area>();
    ASSERT_NE(restored_reload, nullptr);
    {
        std::ifstream input{project / "test_area.caf.json"};
        ASSERT_TRUE(input);
        const auto archive = nlohmann::json::parse(input);
        ASSERT_TRUE(nw::deserialize(restored_reload, archive));
    }
    ASSERT_EQ(restored_reload->tiles.size(), original.size());
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            restored_reload->tiles[index], original[index]));
    }
    restored_reload->clear();
    nwk::objects().destroy(restored_reload->handle());
    workspace.clear();
    std::filesystem::remove_all(project);
}

TEST(ClientAreaTileEdits, PrimaryRulesInsertWallBetweenIncompatibleRoomTerrains)
{
    auto* tileset = nwk::tilesets().load("tin01");
    ASSERT_NE(tileset, nullptr);
    ASSERT_TRUE(tileset->primary_rules_valid);
    ASSERT_EQ(tileset->primary_rules.size(), 25u);

    // Find a pure Wall tile (terrain 0) and a pure Livingroom tile (terrain 1).
    int32_t wall_tile = -1;
    int32_t livingroom_tile = -1;
    for (size_t i = 0; i < tileset->tile_topologies.size(); ++i) {
        if (tileset->grouped_tiles[i] != 0 || !tileset->tile_topologies[i].valid) {
            continue;
        }
        const auto& top = tileset->tile_topologies[i];
        if (top.crosser != std::array<int32_t, 4>{-1, -1, -1, -1}
            || top.height != std::array<int32_t, 4>{0, 0, 0, 0}) {
            continue;
        }
        if (wall_tile < 0 && top.terrain == std::array<int32_t, 4>{0, 0, 0, 0}) {
            wall_tile = static_cast<int32_t>(i);
        }
        if (livingroom_tile < 0 && top.terrain == std::array<int32_t, 4>{1, 1, 1, 1}) {
            livingroom_tile = static_cast<int32_t>(i);
        }
    }
    ASSERT_GE(wall_tile, 0);
    ASSERT_GE(livingroom_tile, 0);

    auto* area = make_area(*tileset, 4, 3);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"tin01"};
    area->tiles.assign(12, distinct_tile(wall_tile, 0, 0, 5));

    // Paint Livingroom on cell (1, 1) -> index 5.
    const std::array<uint32_t, 1> first_cell{5};
    nw::toolset::AreaTileEditBatch first_edit;
    auto built = nw::toolset::build_area_tile_brush_edits(area->handle(),
        first_cell,
        {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 1},
        11, first_edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
        first_edit, nw::toolset::ObjectEditDirection::forward)
            .ok());
    const auto after_livingroom = area->tiles;

    // Paint Kitchen (terrain 2) on adjacent cell (2, 1) -> index 6.
    // Without primary rules, no tin01 tile has both Livingroom and Kitchen
    // corners; primary rule (Kitchen@0, Livingroom@0 -> Wall@0) converts the
    // shared boundary corners back to Wall so both rooms transition through Wall.
    const std::array<uint32_t, 1> second_cell{6};
    nw::toolset::AreaTileEditBatch second_edit;
    built = nw::toolset::build_area_tile_brush_edits(area->handle(),
        second_cell,
        {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 2},
        19, second_edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    EXPECT_GT(second_edit.rows.size(), 1u);

    ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
        second_edit, nw::toolset::ObjectEditDirection::forward)
            .ok());
    ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
        second_edit, nw::toolset::ObjectEditDirection::inverse)
            .ok());
    for (size_t i = 0; i < after_livingroom.size(); ++i) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[i], after_livingroom[i]));
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, PrimaryRulesConvertElevatedWaterNeighborsToGrassInTtr01)
{
    auto* tileset = nwk::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = make_area(*tileset, 4, 4);
    ASSERT_NE(area, nullptr);
    area->tileset_resref = nw::Resref{"ttr01"};
    // Tile 109 is flat Grass@0; raise the whole 4x4 area to height 1, then
    // paint Water (terrain 1) at height 1 and raise an adjacent corner, or
    // paint Grass@0 next to Water@1.
    area->tiles.assign(16, distinct_tile(109, 1, 0, 1));

    // Paint Water (terrain 1) on center cell 5 at height 1.
    const std::array<uint32_t, 1> water_cell{5};
    nw::toolset::AreaTileEditBatch water_edit;
    auto built = nw::toolset::build_area_tile_brush_edits(area->handle(),
        water_cell,
        {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 1},
        42, water_edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
        water_edit, nw::toolset::ObjectEditDirection::forward)
            .ok());
    const auto with_water = area->tiles;

    // Lower corner (1, 1) -> index 1 * 5 + 1 = 6 from height 1 to height 0.
    // Its adjacent corners on cell 5 are Water@1. Primary rule (Water@0, Water@1 -> Grass@1)
    // converts those elevated Water neighbors to Grass@1 so a valid transition tile fits!
    const std::array<uint32_t, 1> corners{6};
    nw::toolset::AreaTileEditBatch lower_edit;
    built = nw::toolset::build_area_tile_height_brush_edits(
        area->handle(), corners, -1, 7, lower_edit);
    ASSERT_TRUE(built.ok()) << built.diagnostic;
    EXPECT_FALSE(lower_edit.rows.empty());

    ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
        lower_edit, nw::toolset::ObjectEditDirection::forward)
            .ok());
    ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
        lower_edit, nw::toolset::ObjectEditDirection::inverse)
            .ok());
    for (size_t i = 0; i < with_water.size(); ++i) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[i], with_water[i]));
    }
    destroy_area(area);
}

TEST(ClientAreaTileEdits, PrimaryRulesRejectMalformedTablesProtectedGroupsAndCyclesAtomically)
{
    auto* malformed = nwk::tilesets().load("bad_rules");
    ASSERT_NE(malformed, nullptr);
    EXPECT_FALSE(malformed->primary_rules_valid);
    auto* bad_area = make_area(*malformed, 1, 1);
    ASSERT_NE(bad_area, nullptr);
    bad_area->tiles = {distinct_tile(0, 0, 0, 1)};
    const std::array<uint32_t, 1> cell_zero{0};
    nw::toolset::AreaTileEditBatch rejected_edit;
    auto built = nw::toolset::build_area_tile_brush_edits(bad_area->handle(),
        cell_zero,
        {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 0},
        1, rejected_edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_NE(built.diagnostic.find("primary rules"), std::string::npos);
    EXPECT_TRUE(rejected_edit.rows.empty());
    destroy_area(bad_area);

    // Cyclic rules must terminate and reject atomically without mutating the area.
    nw::Tileset cyclic_set;
    cyclic_set.tiles.resize(2);
    cyclic_set.terrains = {{"A"}, {"B"}};
    cyclic_set.tile_topologies.resize(2);
    cyclic_set.grouped_tiles = {0, 0};
    cyclic_set.tile_topologies[0].terrain.fill(0);
    cyclic_set.tile_topologies[0].valid = true;
    cyclic_set.tile_topologies[1].terrain.fill(1);
    cyclic_set.tile_topologies[1].valid = true;
    cyclic_set.primary_rules_valid = true;
    cyclic_set.primary_rules = {
        {.placed = 0, .placed_height = 0, .adjacent = 0, .adjacent_height = 0, .changed = 1, .changed_height = 0},
        {.placed = 1, .placed_height = 0, .adjacent = 1, .adjacent_height = 0, .changed = 0, .changed_height = 0},
        {.placed = 0, .placed_height = 0, .adjacent = 1, .adjacent_height = 0, .changed = 0, .changed_height = 0},
        {.placed = 1, .placed_height = 0, .adjacent = 0, .adjacent_height = 0, .changed = 1, .changed_height = 0},
    };
    auto* cyclic_area = make_area(cyclic_set, 2, 2);
    ASSERT_NE(cyclic_area, nullptr);
    cyclic_area->tiles.assign(4, distinct_tile(0, 0, 0, 1));
    const auto cyclic_original = cyclic_area->tiles;
    built = nw::toolset::build_area_tile_brush_edits(cyclic_area->handle(),
        cell_zero,
        {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 0},
        1, rejected_edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_NE(built.diagnostic.find("work limit"), std::string::npos);
    EXPECT_TRUE(rejected_edit.rows.empty());
    for (size_t i = 0; i < cyclic_original.size(); ++i) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            cyclic_area->tiles[i], cyclic_original[i]));
    }
    destroy_area(cyclic_area);

    // A primary rule that would alter a corner of an unselected placed group
    // must reject atomically with the group-split diagnostic.
    auto* rules_set = nwk::tilesets().load("rules_primary");
    ASSERT_NE(rules_set, nullptr);
    nw::Tileset group_rule_set = *rules_set;
    group_rule_set.tiles.push_back(group_rule_set.tiles[1]);
    group_rule_set.tile_topologies.push_back(group_rule_set.tile_topologies[1]);
    group_rule_set.grouped_tiles.resize(group_rule_set.tiles.size(), 0);
    const int32_t group_tile_id = static_cast<int32_t>(group_rule_set.tiles.size() - 1);
    group_rule_set.grouped_tiles[static_cast<size_t>(group_tile_id)] = 1;
    group_rule_set.groups = {{
        .rows = 1,
        .columns = 1,
        .tile_offset = 0,
        .tile_count = 1,
    }};
    group_rule_set.group_tile_ids = {group_tile_id};

    auto* group_area = make_area(group_rule_set, 3, 1);
    ASSERT_NE(group_area, nullptr);
    group_area->tiles = {
        distinct_tile(2, 0, 0, 1),
        distinct_tile(4, 0, 2, 2),
        distinct_tile(group_tile_id, 0, 0, 3),
    };
    built = nw::toolset::build_area_tile_brush_edits(group_area->handle(),
        cell_zero,
        {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 0},
        1, rejected_edit);
    EXPECT_EQ(built.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_NE(built.diagnostic.find("split a placed tileset group"),
        std::string::npos)
        << built.diagnostic;
    EXPECT_TRUE(rejected_edit.rows.empty());
    destroy_area(group_area);
}

TEST(ClientAreaTileEdits, PrimaryRulesSupportElevationsOrderedGesturesGroupPlacementAndEraser)
{
    auto* rules_set = nwk::tilesets().load("rules_primary");
    ASSERT_NE(rules_set, nullptr);
    ASSERT_TRUE(rules_set->primary_rules_valid);

    // 1. Verify normalized height shifts across negative, zero, and positive
    // base elevations, and all 4 rotations on a 3x3 grid.
    for (const int32_t base_elevation : {-2, 0, 5}) {
        SCOPED_TRACE(base_elevation);
        auto* area = make_area(*rules_set, 3, 3);
        ASSERT_NE(area, nullptr);
        area->tileset_resref = nw::Resref{"rules_primary"};
        area->tiles.resize(9);
        for (uint32_t i = 0; i < 9; ++i) {
            area->tiles[i] = distinct_tile(0, base_elevation, 0, static_cast<uint8_t>(i + 1));
        }

        // Paint FloorB (terrain 1) on center cell 4. Primary rule (FloorB@0, FloorA@0 -> Wall@0)
        // turns the outer ring of corners on the 3x3 area into Wall@base_elevation,
        // requiring all 4 rotations of the FloorB/Wall transition tiles around cell 4.
        const std::array<uint32_t, 1> center{4};
        nw::toolset::AreaTileEditBatch edit;
        const auto built = nw::toolset::build_area_tile_brush_edits(area->handle(),
            center,
            {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 1},
            17, edit);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        EXPECT_EQ(edit.rows.size(), 9u);

        // Repeating with the same inputs and seed produces the exact same rows.
        nw::toolset::AreaTileEditBatch repeat_edit;
        const auto repeat_built = nw::toolset::build_area_tile_brush_edits(
            area->handle(), center,
            {.kind = nw::toolset::AreaTileBrushKind::terrain, .value = 1},
            17, repeat_edit);
        ASSERT_TRUE(repeat_built.ok());
        ASSERT_EQ(repeat_edit.rows.size(), edit.rows.size());
        for (size_t i = 0; i < edit.rows.size(); ++i) {
            EXPECT_EQ(repeat_edit.rows[i].tile_index, edit.rows[i].tile_index);
            EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
                repeat_edit.rows[i].before, edit.rows[i].before));
            EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
                repeat_edit.rows[i].after, edit.rows[i].after));
        }

        const auto before_tiles = area->tiles;
        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            edit, nw::toolset::ObjectEditDirection::forward)
                .ok());
        for (size_t i = 0; i < area->tiles.size(); ++i) {
            EXPECT_EQ(area->tiles[i].height, base_elevation);
            // Non-geometry fields (lights, animloop) must be preserved across every propagated row.
            EXPECT_EQ(area->tiles[i].mainlight1, before_tiles[i].mainlight1);
            EXPECT_EQ(area->tiles[i].srclight1, before_tiles[i].srclight1);
            EXPECT_EQ(area->tiles[i].animloop1, before_tiles[i].animloop1);
        }
        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            edit, nw::toolset::ObjectEditDirection::inverse)
                .ok());
        for (size_t i = 0; i < before_tiles.size(); ++i) {
            EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
                area->tiles[i], before_tiles[i]));
        }
        destroy_area(area);
    }

    // 2. Group placement: placing a 1x1 FloorA group onto a 3x3 FloorB map
    // keeps the fixed group tile intact while primary rules convert the outer
    // FloorB corners to Wall so all 8 surrounding transition cells fit.
    {
        nw::Tileset group_set = *rules_set;
        group_set.tiles.push_back(group_set.tiles[0]);
        group_set.tile_topologies.push_back(group_set.tile_topologies[0]);
        group_set.grouped_tiles.resize(group_set.tiles.size(), 0);
        const int32_t group_tile_id = static_cast<int32_t>(group_set.tiles.size() - 1);
        group_set.grouped_tiles[static_cast<size_t>(group_tile_id)] = 1;
        group_set.groups = {{
            .rows = 1,
            .columns = 1,
            .tile_offset = 0,
            .tile_count = 1,
        }};
        group_set.group_tile_ids = {group_tile_id};
        group_set.default_terrain = 1; // FloorB

        auto* area = make_area(group_set, 3, 3);
        ASSERT_NE(area, nullptr);
        area->tiles.assign(9, distinct_tile(1, 0, 0, 2)); // All FloorB

        const std::array<uint32_t, 1> anchor{4};
        nw::toolset::AreaTileEditBatch place_edit;
        auto built = nw::toolset::build_area_tile_brush_edits(area->handle(),
            anchor,
            {.kind = nw::toolset::AreaTileBrushKind::group, .value = 0},
            23, place_edit);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        EXPECT_EQ(place_edit.rows.size(), 9u);
        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            place_edit, nw::toolset::ObjectEditDirection::forward)
                .ok());
        EXPECT_EQ(area->tiles[4].id, group_tile_id);

        // Erasing the placed group back to FloorB (terrain 1) or erasing a
        // cell in a FloorA map to FloorB propagates primary rules to neighbors.
        destroy_area(area);
    }

    // 3. Eraser: erasing center cell 1 in a 3x1 FloorA map using terrain 1 (FloorB)
    // converts corners x=0 and x=3 to Wall so cells 0 and 2 become FloorA/Wall
    // transitions while cell 1 becomes pure FloorB.
    {
        auto* area = make_area(*rules_set, 3, 1);
        ASSERT_NE(area, nullptr);
        area->tiles = {
            distinct_tile(0, 0, 0, 1),
            distinct_tile(0, 0, 0, 2),
            distinct_tile(0, 0, 0, 3),
        };
        const auto original = area->tiles;
        const std::array<uint32_t, 1> erase_target{1};
        nw::toolset::AreaTileEraseEditBatch erase_edit;
        const auto built = nw::toolset::build_area_tile_erase_edits(
            area->handle(), erase_target, 1, 13, erase_edit);
        ASSERT_TRUE(built.ok()) << built.diagnostic;
        EXPECT_EQ(erase_edit.tiles.rows.size(), 3u);
        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            erase_edit.tiles, nw::toolset::ObjectEditDirection::forward)
                .ok());
        EXPECT_EQ(area->tiles[1].id, 1);
        ASSERT_TRUE(nw::toolset::apply_area_tile_edits(
            erase_edit.tiles, nw::toolset::ObjectEditDirection::inverse)
                .ok());
        for (size_t i = 0; i < original.size(); ++i) {
            EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
                area->tiles[i], original[i]));
        }
        destroy_area(area);
    }

    // 4. Ordered multi-target gesture: when two raised corners compete for a
    // shared neighbor corner via asymmetric rules, reversing gesture order
    // deterministically changes the winning terrain on the shared neighbor.
    {
        nw::Tileset order_set;
        order_set.has_height_transition = true;
        order_set.terrains = {{"A"}, {"B"}, {"C"}};
        // Generate flat and sloped tiles for every combination of A/B/C at heights 0..1
        // so tile fitting succeeds for any resolved topology.
        for (int32_t t0 = 0; t0 < 3; ++t0) {
            for (int32_t t1 = 0; t1 < 3; ++t1) {
                for (int32_t h0 = 0; h0 <= 1; ++h0) {
                    for (int32_t h1 = 0; h1 <= 1; ++h1) {
                        nw::Tile tile_def;
                        tile_def.model = "m";
                        order_set.tiles.push_back(tile_def);
                        nw::TilesetTileTopology top;
                        top.terrain = {t0, t1, t0, t1};
                        top.height = {h0, h1, h0, h1};
                        top.crosser = {-1, -1, -1, -1};
                        top.valid = true;
                        order_set.tile_topologies.push_back(top);
                        order_set.grouped_tiles.push_back(0);
                    }
                }
            }
        }
        order_set.primary_rules_valid = true;
        order_set.primary_rules = {
            // Raising A@1 next to C@0 converts C@0 -> A@0
            {.placed = 0, .placed_height = 1, .adjacent = 2, .adjacent_height = 0, .changed = 0, .changed_height = 0},
            // Raising B@1 next to A@0 converts A@0 -> B@0
            {.placed = 1, .placed_height = 1, .adjacent = 0, .adjacent_height = 0, .changed = 1, .changed_height = 0},
            // Raising B@1 next to C@0 converts C@0 -> B@0
            {.placed = 1, .placed_height = 1, .adjacent = 2, .adjacent_height = 0, .changed = 1, .changed_height = 0},
            // Raising A@1 next to B@0 converts B@0 -> A@0
            {.placed = 0, .placed_height = 1, .adjacent = 1, .adjacent_height = 0, .changed = 0, .changed_height = 0},
        };

        // 2x1 area: corners x=0 (A@0), x=1 (C@0), x=2 (B@0).
        int32_t tile_ac = -1;
        int32_t tile_cb = -1;
        for (size_t i = 0; i < order_set.tile_topologies.size(); ++i) {
            const auto& top = order_set.tile_topologies[i];
            if (top.height == std::array<int32_t, 4>{0, 0, 0, 0}) {
                if (top.terrain == std::array<int32_t, 4>{0, 2, 0, 2}) {
                    tile_ac = static_cast<int32_t>(i);
                }
                if (top.terrain == std::array<int32_t, 4>{2, 1, 2, 1}) {
                    tile_cb = static_cast<int32_t>(i);
                }
            }
        }
        ASSERT_GE(tile_ac, 0);
        ASSERT_GE(tile_cb, 0);

        auto* area = make_area(order_set, 2, 1);
        ASSERT_NE(area, nullptr);
        area->tiles = {
            distinct_tile(tile_ac, 0, 0, 1),
            distinct_tile(tile_cb, 0, 0, 2),
        };

        // Raise left corners (0, 3) (A) then right corners (2, 5) (B):
        // Left corners convert middle corners (1, 4) from C -> A, then right corners convert (1, 4) from A -> B.
        const std::array<uint32_t, 4> forward_order{0, 3, 2, 5};
        nw::toolset::AreaTileEditBatch forward_batch;
        const auto forward_built = nw::toolset::build_area_tile_height_brush_edits(
            area->handle(), forward_order, 1, 1, forward_batch);
        ASSERT_TRUE(forward_built.ok()) << forward_built.diagnostic;

        // Raise right corners (2, 5) (B) then left corners (0, 3) (A):
        // Right corners convert middle corners (1, 4) from C -> B, then left corners convert (1, 4) from B -> A.
        const std::array<uint32_t, 4> reverse_order{2, 5, 0, 3};
        nw::toolset::AreaTileEditBatch reverse_batch;
        const auto reverse_built = nw::toolset::build_area_tile_height_brush_edits(
            area->handle(), reverse_order, 1, 1, reverse_batch);
        ASSERT_TRUE(reverse_built.ok()) << reverse_built.diagnostic;

        ASSERT_EQ(forward_batch.rows.size(), 2u);
        ASSERT_EQ(reverse_batch.rows.size(), 2u);
        EXPECT_NE(forward_batch.rows[0].after.id, reverse_batch.rows[0].after.id);
        destroy_area(area);
    }
}
