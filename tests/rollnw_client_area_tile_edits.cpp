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

    const auto undone = nw::toolset::apply_area_tile_edits(
        batch, nw::toolset::ObjectEditDirection::inverse);
    ASSERT_TRUE(undone.ok()) << undone.diagnostic;
    ASSERT_EQ(area->tiles.size(), original.size());
    for (size_t index = 0; index < original.size(); ++index) {
        EXPECT_TRUE(nw::toolset::area_tile_rows_equal(
            area->tiles[index], original[index]));
    }

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
    ASSERT_EQ(palette.matches.size(), 3u);
    for (const uint32_t row_index : palette.matches) {
        ASSERT_LT(row_index, palette.rows.size());
        EXPECT_EQ(palette.rows[row_index].kind,
            nw::toolset::AreaTilePaletteRowKind::folder);
        EXPECT_EQ(palette.rows[row_index].parent, palette.root_folder);
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
    EXPECT_NE(raise, palette.rows.end());
    EXPECT_EQ(lower, palette.rows.end());
    EXPECT_NE(eraser, palette.rows.end());
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
