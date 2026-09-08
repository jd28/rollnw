#include <gtest/gtest.h>

#include "../tools/client/area_navigation.hpp"
#include "../tools/client/object_edits.hpp"
#include "../tools/client/preview_session.hpp"
#include "../tools/client/workspace.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>

#include <array>
#include <chrono>
#include <limits>

namespace {

class ClientAreaNavigation : public testing::Test {
protected:
    void SetUp() override
    {
        auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
        ASSERT_NE(module, nullptr);
        area = module->get_area(0);
        ASSERT_NE(area, nullptr);
        const auto started = nw::toolset::start_toolset_preview(preview, {
                                                                             .area = area->handle(),
                                                                             .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
                                                                             .spawn_position = module->entry_position,
                                                                             .camera = {.focus = module->entry_position},
                                                                         });
        ASSERT_TRUE(started.ok()) << started.diagnostic;
        row = *nw::kernel::objects().components().find_spatial(started.actor);
    }

    nw::toolset::ToolsetPreviewSession preview;
    nw::Area* area = nullptr;
    nw::ObjectSpatialState row;
};

} // namespace

TEST_F(ClientAreaNavigation, ReusesGestureSnapshotAndRejectsFloatingCreature)
{
    nw::toolset::AreaPlacementNavigation navigation;
    std::array rows{row};
    const auto started = std::chrono::steady_clock::now();
    const auto valid = nw::toolset::validate_area_placements(navigation, area->handle(), rows);
    const auto built = std::chrono::steady_clock::now();
    ASSERT_TRUE(valid.ok()) << valid.diagnostic;
    ASSERT_TRUE(navigation.world_ready);
    const auto revision = navigation.revision;
    const auto* vertices = navigation.source.geometry.surface_vertices.data();
    for (size_t index = 0; index < 1000; ++index) {
        ASSERT_TRUE(nw::toolset::validate_area_placements(navigation, area->handle(), rows).ok());
    }
    const auto queried = std::chrono::steady_clock::now();
    RecordProperty("cold_build_us", std::chrono::duration_cast<std::chrono::microseconds>(built - started).count());
    RecordProperty("warm_1000_queries_us", std::chrono::duration_cast<std::chrono::microseconds>(queried - built).count());
    EXPECT_EQ(navigation.revision, revision);
    EXPECT_EQ(navigation.source.geometry.surface_vertices.data(), vertices);
    ASSERT_TRUE(nw::toolset::collect_placement_navigation_debug(navigation));
    EXPECT_FALSE(navigation.debug_triangles.empty());

    rows[0].position.z += 1.0f;
    const auto floating = nw::toolset::validate_area_placements(navigation, area->handle(), rows);
    EXPECT_EQ(floating.status, nw::nav::NavStatus::off_mesh);
    EXPECT_EQ(floating.input_index, 0u);
    EXPECT_EQ(navigation.revision, revision);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(row.owner)->position, row.position);
}

TEST_F(ClientAreaNavigation, UnknownRadiusAndMalformedInputsRejectWithoutNavigation)
{
    nw::toolset::AreaPlacementNavigation navigation;
    auto* visual = nw::kernel::objects().components().find_visual(row.owner);
    ASSERT_NE(visual, nullptr);
    const auto appearance = visual->appearance;
    visual->appearance = std::numeric_limits<int32_t>::max();
    std::array rows{row};
    auto result = nw::toolset::validate_area_placements(navigation, area->handle(), rows);
    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.diagnostic.find("clearance"), std::string::npos);
    EXPECT_FALSE(navigation.source_attempted);
    visual->appearance = appearance;

    rows[0].position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, area->handle(), rows).ok());
    rows[0] = row;
    rows[0].scale = {0.0f, 1.0f, 1.0f};
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, area->handle(), rows).ok());
    const std::array duplicates{row, row};
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, area->handle(), duplicates).ok());
    EXPECT_FALSE(navigation.source_attempted);
}

TEST_F(ClientAreaNavigation, ChangedEpochInvalidatesTheGestureBeforeAnotherQuery)
{
    nw::toolset::AreaPlacementNavigation navigation;
    const std::array rows{row};
    ASSERT_TRUE(nw::toolset::validate_area_placements(navigation, area->handle(), rows).ok());
    const auto revision = navigation.revision;
    auto tiles = std::move(area->tiles);

    // Use a real edit to advance the toolset epoch after the source changes.
    auto* marker = nw::kernel::objects().make<nw::Placeable>();
    auto* spatial = nw::kernel::objects().components().get_or_create_spatial(marker->handle());
    const nw::toolset::ObjectTransformState before{spatial->position, spatial->orientation, spatial->scale};
    auto after = before;
    after.position.z = 1.0f;
    ASSERT_TRUE(nw::toolset::apply_object_transform_edit(
        {marker->handle(), before, after}, nw::toolset::ObjectEditDirection::forward)
            .ok());
    const auto invalid = nw::toolset::validate_area_placements(navigation, area->handle(), rows);
    EXPECT_FALSE(invalid.ok());
    EXPECT_FALSE(navigation.world_ready);
    EXPECT_GT(navigation.revision, revision);
    EXPECT_FALSE(nw::toolset::collect_placement_navigation_debug(navigation));
    area->tiles = std::move(tiles);
    nw::kernel::objects().destroy(marker->handle());
}

TEST_F(ClientAreaNavigation, StaleAreaAndObjectHandlesCannotReuseTheGesture)
{
    nw::toolset::AreaPlacementNavigation navigation;
    std::array rows{row};
    ASSERT_TRUE(nw::toolset::validate_area_placements(navigation, area->handle(), rows).ok());
    auto stale_area = area->handle();
    ++stale_area.version;
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, stale_area, rows).ok());
    ++rows[0].owner.version;
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, area->handle(), rows).ok());
    rows[0] = row;
    rows[0].area = nw::object_invalid;
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, area->handle(), rows).ok());
}

TEST_F(ClientAreaNavigation, PlaceablesAcceptAuthoredHeightWithoutWalkmesh)
{
    auto* empty_area = nw::kernel::objects().make<nw::Area>();
    empty_area->width = empty_area->height = 1;
    auto* object = nw::kernel::objects().make<nw::Placeable>();
    auto* spatial = nw::kernel::objects().components().get_or_create_spatial(object->handle());
    ASSERT_NE(spatial, nullptr);
    spatial->area = empty_area->handle().id;
    spatial->position = {5.0f, 5.0f, 4.0f};
    const std::array rows{*spatial};
    nw::toolset::AreaPlacementNavigation navigation;
    EXPECT_TRUE(nw::toolset::validate_area_placements(navigation, empty_area->handle(), rows).ok());
    EXPECT_FALSE(navigation.source_attempted);
    EXPECT_FALSE(navigation.world_ready);

    nw::toolset::CommandContext context;
    const std::array objects{object->handle()};
    auto placed = nw::toolset::place_area_objects(empty_area->handle(), objects, "Place decoration", context);
    ASSERT_TRUE(placed.ok()) << placed.message;
    ASSERT_TRUE(placed.undo_action);
    EXPECT_EQ(empty_area->placeables.size(), 1u);
    EXPECT_TRUE(placed.undo_action->undo(context).ok());
    EXPECT_TRUE(placed.undo_action->redo(context).ok());
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(object->handle())->position.z, 4.0f);
    nw::kernel::objects().destroy(empty_area->handle());
}

TEST_F(ClientAreaNavigation, ItemsPlaceMoveDuplicateAndDeleteWithoutNavigation)
{
    auto* empty_area = nw::kernel::objects().make<nw::Area>();
    ASSERT_NE(empty_area, nullptr);
    empty_area->width = empty_area->height = 1;
    const std::array placements{nw::toolset::AreaObjectBlueprintPlacement{
        .resource = nw::Resource{nw::Resref{"cloth028"}, nw::ResourceType::uti},
        .transform = {.position = {5.0f, 5.0f, 4.0f}},
    }};
    const auto loaded = nw::toolset::load_area_object_blueprints(empty_area->handle(), placements);
    ASSERT_TRUE(loaded.ok()) << loaded.diagnostic;
    ASSERT_EQ(loaded.objects.size(), 1u);
    const auto item = loaded.objects.front();
    const auto spatial = *nw::kernel::objects().components().find_spatial(item);
    std::array rows{spatial};
    nw::toolset::AreaPlacementNavigation navigation;
    EXPECT_TRUE(nw::toolset::validate_area_placements(navigation, empty_area->handle(), rows).ok());
    EXPECT_FALSE(navigation.source_attempted);
    EXPECT_FALSE(navigation.world_ready);
    rows[0].position.x = 11.0f;
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, empty_area->handle(), rows).ok());
    rows[0].position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(nw::toolset::validate_area_placements(navigation, empty_area->handle(), rows).ok());

    nw::toolset::CommandContext context;
    context.area_object = empty_area->handle();
    auto placed = nw::toolset::place_area_objects(empty_area->handle(), loaded.objects, "Drop item", context);
    ASSERT_TRUE(placed.ok()) << placed.message;
    ASSERT_EQ(empty_area->items.size(), 1u);
    EXPECT_EQ(empty_area->items.front()->handle(), item);
    EXPECT_EQ(nw::toolset::place_area_objects(empty_area->handle(), loaded.objects, "Drop again", context).status,
        nw::toolset::CommandStatus::rejected);

    const nw::toolset::ObjectTransformState before{spatial.position, spatial.orientation, spatial.scale};
    auto after = before;
    after.position.z += 1.0f;
    auto moved = nw::toolset::commit_object_transform_edit({item, before, after}, "Raise item", context);
    ASSERT_TRUE(moved.ok()) << moved.message;
    ASSERT_TRUE(moved.undo_action);
    EXPECT_TRUE(moved.undo_action->undo(context).ok());
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(item)->position, before.position);
    EXPECT_TRUE(moved.undo_action->redo(context).ok());
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(item)->position, after.position);

    auto duplicated = nw::toolset::duplicate_area_objects(empty_area->handle(), loaded.objects, "Copy item", context);
    ASSERT_TRUE(duplicated.ok()) << duplicated.message;
    ASSERT_EQ(empty_area->items.size(), 2u);
    const auto clone = empty_area->items.back()->handle();
    EXPECT_NE(clone, item);
    EXPECT_EQ(empty_area->items.back()->resref, "cloth028");
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(clone)->position,
        after.position + glm::vec3(1.5f, 1.5f, 0.0f));
    ASSERT_TRUE(duplicated.undo_action);
    EXPECT_TRUE(duplicated.undo_action->undo(context).ok());
    EXPECT_EQ(empty_area->items.size(), 1u);
    EXPECT_TRUE(duplicated.undo_action->redo(context).ok());
    ASSERT_EQ(empty_area->items.size(), 2u);
    EXPECT_EQ(empty_area->items.back()->handle(), clone);

    const std::array selected{clone, item}; // Deliberately reverse membership order.
    auto deleted = nw::toolset::delete_area_objects(empty_area->handle(), selected, "Delete items", context);
    ASSERT_TRUE(deleted.ok()) << deleted.message;
    EXPECT_TRUE(empty_area->items.empty());
    ASSERT_TRUE(deleted.undo_action);
    EXPECT_TRUE(deleted.undo_action->undo(context).ok());
    ASSERT_EQ(empty_area->items.size(), 2u);
    EXPECT_EQ(empty_area->items.front()->handle(), item);
    EXPECT_EQ(empty_area->items.back()->handle(), clone);
    EXPECT_TRUE(deleted.undo_action->redo(context).ok());
    EXPECT_TRUE(empty_area->items.empty());
    deleted.undo_action.reset();
    EXPECT_FALSE(nw::kernel::objects().valid(item));
    EXPECT_FALSE(nw::kernel::objects().valid(clone));
    nw::kernel::objects().destroy(empty_area->handle());
}

TEST_F(ClientAreaNavigation, AdmissionRejectsWholeMixedBatchWithoutTakingOwnership)
{
    auto* object = nw::kernel::objects().make<nw::Placeable>();
    auto* spatial = nw::kernel::objects().components().get_or_create_spatial(object->handle());
    spatial->area = area->handle().id;
    spatial->position = row.position + glm::vec3{0.0f, 0.0f, 4.0f};
    nw::kernel::objects().components().set_position(row.owner, row.position + glm::vec3{0.0f, 0.0f, 2.0f});
    const auto creatures = area->creatures.size();
    const auto placeables = area->placeables.size();
    const auto epoch = nw::toolset::object_mutation_state().epoch;
    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("area:test", "Test", nw::toolset::WorkspaceTabKind::area);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    const std::array objects{object->handle(), row.owner};
    const auto placed = nw::toolset::place_area_objects(area->handle(), objects, "Invalid placement", context);
    EXPECT_EQ(placed.status, nw::toolset::CommandStatus::rejected);
    EXPECT_FALSE(placed.undo_action);
    EXPECT_FALSE(workspace.active_tab()->dirty);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch);
    EXPECT_EQ(area->creatures.size(), creatures);
    EXPECT_EQ(area->placeables.size(), placeables);
    EXPECT_TRUE(nw::kernel::objects().valid(row.owner));
    EXPECT_TRUE(nw::kernel::objects().valid(object->handle()));
    nw::kernel::objects().destroy(object->handle());
}

TEST_F(ClientAreaNavigation, TransformAndRedoUseFreshNavigationAndPreserveUndo)
{
    nw::toolset::CommandContext context;
    context.area_object = area->handle();
    const nw::toolset::ObjectTransformState before{row.position, row.orientation, row.scale};
    auto invalid = before;
    invalid.position.z += 2.0f;
    const auto rejected = nw::toolset::commit_object_transform_edit(
        {row.owner, before, invalid}, "Invalid move", context);
    EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(row.owner)->position, row.position);

    auto after = before;
    after.orientation = {-before.orientation.y, before.orientation.x, before.orientation.z};
    auto changed = nw::toolset::commit_object_transform_edit({row.owner, before, after}, "Rotate", context);
    ASSERT_TRUE(changed.ok()) << changed.message;
    ASSERT_TRUE(changed.undo_action);
    EXPECT_TRUE(changed.undo_action->undo(context).ok());

    auto* visual = nw::kernel::objects().components().find_visual(row.owner);
    const auto appearance = visual->appearance;
    visual->appearance = std::numeric_limits<int32_t>::max();
    EXPECT_FALSE(changed.undo_action->redo(context).ok());
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(row.owner)->orientation, before.orientation);
    visual->appearance = appearance;
    EXPECT_TRUE(changed.undo_action->redo(context).ok());
}
