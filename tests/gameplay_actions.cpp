#include <gtest/gtest.h>

#include <nw/gameplay/actions.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/nav/NavTileBuild.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <array>
#include <limits>

namespace {

using namespace nw::gameplay;

class GameplayActions : public ::testing::Test {
protected:
    nw::Area* area = nullptr;
    std::array<ActorActionState, 2> states;
    std::array<ActionMovement, 2> motions;
    std::array<ActorActionResult, 2> results;
    nw::nav::NavAreaBuildSource source;
    nw::nav::NavWorldState navigation;

    void SetUp() override
    {
        area = nw::kernel::objects().make<nw::Area>();
        ASSERT_NE(area, nullptr);
        for (size_t index = 0; index < states.size(); ++index) {
            auto* actor = nw::kernel::objects().make<nw::Creature>();
            ASSERT_NE(actor, nullptr);
            area->creatures.push_back(actor);
            actor->inventory() = nw::Inventory{1, 2, 2, actor};
            states[index].actor = actor->handle();
            ASSERT_TRUE(nw::kernel::objects().components().set_area(actor->handle(), area->handle().id));
            position(actor->handle(), {2.0f, 2.0f + static_cast<float>(index), 0.0f});
        }
        source.width = source.height = 1;
        source.surface_walkable = {0u, 1u};
        source.surface_vertices = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {10, 10, 0}};
        source.surface_indices = {0, 2, 1, 1, 2, 3};
        source.surface_ids = {1, 1};
        nw::nav::NavTileBuildConfig config;
        config.erosion_cells = 1;
        nw::nav::NavTiledWorldBuildStats stats;
        ASSERT_EQ(nw::nav::build_tiled_nav_world(source, {}, config, navigation, stats), nw::nav::NavStatus::ok);
    }

    void TearDown() override
    {
        area->clear();
        nw::kernel::objects().destroy(area->handle());
    }

    void position(nw::ObjectHandle object, glm::vec3 value)
    {
        ASSERT_TRUE(nw::kernel::objects().components().set_position(object, value));
    }

    nw::Item* item(glm::vec3 at = {8, 2, 0})
    {
        auto* result = nw::kernel::objects().make<nw::Item>();
        area->items.push_back(result);
        EXPECT_TRUE(nw::kernel::objects().components().set_item_layout(result->handle(), 1, 1));
        EXPECT_TRUE(nw::kernel::objects().components().set_area(result->handle(), area->handle().id));
        position(result->handle(), at);
        return result;
    }

    void submit(ActorActionRequest request)
    {
        std::array<ActorActionResult, 1> accepted;
        ASSERT_TRUE(submit_actor_actions(states, {&request, 1}, accepted));
        ASSERT_EQ(accepted[0].status, ActionStatus::accepted);
    }

    void prepare()
    {
        ASSERT_TRUE(prepare_actor_actions(area->handle(), states, motions, results));
    }

    void complete(ActionMovementStatus status = ActionMovementStatus::moving)
    {
        const std::array movement{status, status};
        ASSERT_TRUE(complete_actor_actions(area->handle(), navigation, 2.0f, states, movement, results));
    }
};

TEST_F(GameplayActions, ApproachRequestsMovementAndCommitsOnlyOnce)
{
    auto* target = item();
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    prepare();
    EXPECT_EQ(motions[0].kind, ActionMovementKind::move_to);
    EXPECT_EQ(motions[0].destination, glm::vec3(8, 2, 0));
    complete();
    EXPECT_EQ(results[0].status, ActionStatus::none);
    EXPECT_EQ(area->items.size(), 1u);
    prepare();
    EXPECT_EQ(motions[0].kind, ActionMovementKind::none);
    position(states[0].actor, {7, 2, 0});
    complete();
    ASSERT_EQ(results[0].status, ActionStatus::applied);
    EXPECT_TRUE(results[0].changed);
    EXPECT_EQ(states[0].phase, ActionPhase::finished);
    EXPECT_TRUE(area->items.empty());

    // Even if the same item returns to the world, the completed action cannot
    // execute its transfer again on a later tick.
    const std::array drop{nw::ItemTransfer{target->handle(), nw::ItemTransferDirection::drop}};
    ASSERT_TRUE(nw::transfer_area_items(area->handle(), states[0].actor, navigation, 2.0f, drop).ok());
    complete(ActionMovementStatus::arrived);
    EXPECT_EQ(results[0].status, ActionStatus::none);
    EXPECT_EQ(area->items.size(), 1u);
}

TEST_F(GameplayActions, CompetingActorsHaveOneOwnerInStableRowOrder)
{
    auto* target = item({3, 2, 0});
    const std::array requests{
        ActorActionRequest{.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()},
        ActorActionRequest{.actor_index = 1, .kind = ActionKind::pickup, .target = target->handle()},
    };
    ASSERT_TRUE(submit_actor_actions(states, requests, results));
    prepare();
    complete();
    EXPECT_EQ(results[0].status, ActionStatus::applied);
    EXPECT_EQ(results[1].status, ActionStatus::rejected);
    EXPECT_EQ(results[1].transfer_status, nw::ItemTransferStatus::invalid_ownership);
    EXPECT_TRUE(nw::kernel::objects().get<nw::Creature>(states[0].actor)->inventory().has_item(target));
    EXPECT_FALSE(nw::kernel::objects().get<nw::Creature>(states[1].actor)->inventory().has_item(target));
    EXPECT_TRUE(area->items.empty());
}

TEST_F(GameplayActions, CancellationStopsTheOwnedApproachWithoutTransferring)
{
    auto* target = item();
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    prepare();
    submit({.actor_index = 0, .kind = ActionKind::cancel});
    prepare();
    EXPECT_EQ(motions[0].kind, ActionMovementKind::stop);
    EXPECT_EQ(results[0].status, ActionStatus::cancelled);
    position(states[0].actor, {7, 2, 0});
    complete(ActionMovementStatus::arrived);
    EXPECT_EQ(results[0].status, ActionStatus::none);
    EXPECT_EQ(area->items.size(), 1u);
}

TEST_F(GameplayActions, ReplacementMoveDoesNotLeavePickupWaiting)
{
    auto* target = item();
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    prepare();
    submit({.actor_index = 0, .kind = ActionKind::move_to, .destination = {7, 2, 0}});
    prepare();
    EXPECT_EQ(motions[0].destination, glm::vec3(7, 2, 0));
    position(states[0].actor, {7, 2, 0});
    complete(ActionMovementStatus::arrived);
    EXPECT_EQ(results[0].status, ActionStatus::applied);
    EXPECT_FALSE(results[0].changed);
    EXPECT_EQ(area->items.size(), 1u);
}

TEST_F(GameplayActions, MovedOrRemovedTargetsAreRevalidated)
{
    auto* target = item();
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    prepare();
    position(target->handle(), {7, 3, 0});
    prepare();
    EXPECT_EQ(motions[0].kind, ActionMovementKind::move_to);
    EXPECT_EQ(motions[0].destination, glm::vec3(7, 3, 0));
    area->items.clear();
    nw::kernel::objects().destroy(target->handle());
    prepare();
    EXPECT_EQ(motions[0].kind, ActionMovementKind::stop);
    EXPECT_EQ(results[0].status, ActionStatus::rejected);
    EXPECT_EQ(results[0].transfer_status, nw::ItemTransferStatus::invalid_ownership);
}

TEST_F(GameplayActions, FailedAndPartialApproachesDoNotTransfer)
{
    auto* target = item();
    const ActorActionRequest pickup{.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()};
    submit(pickup);
    prepare();
    complete(ActionMovementStatus::failed);
    EXPECT_EQ(results[0].status, ActionStatus::movement_failed);
    EXPECT_EQ(area->items.size(), 1u);
    submit(pickup);
    prepare();
    complete(ActionMovementStatus::arrived);
    EXPECT_EQ(results[0].status, ActionStatus::rejected);
    EXPECT_EQ(results[0].transfer_status, nw::ItemTransferStatus::out_of_reach);
    EXPECT_EQ(area->items.size(), 1u);
}

TEST_F(GameplayActions, MalformedRequestsAndBatchesPreserveActiveWork)
{
    auto* target = item();
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    const std::array invalid{
        ActorActionRequest{.actor_index = 3},
        ActorActionRequest{.actor_index = 0, .kind = ActionKind::move_to, .destination = {std::numeric_limits<float>::quiet_NaN(), 0, 0}},
    };
    ASSERT_TRUE(submit_actor_actions(states, invalid, results));
    EXPECT_EQ(results[0].status, ActionStatus::invalid_input);
    EXPECT_EQ(results[1].status, ActionStatus::invalid_input);
    EXPECT_EQ(states[0].kind, ActionKind::pickup);
    EXPECT_EQ(states[0].phase, ActionPhase::requested);
    EXPECT_FALSE(prepare_actor_actions(area->handle(), states, {}, results));
    EXPECT_EQ(states[0].phase, ActionPhase::requested);
    const std::array motion{ActionMovementStatus::arrived, ActionMovementStatus::arrived};
    EXPECT_FALSE(complete_actor_actions(area->handle(), navigation, -1.0f, states, motion, results));
    EXPECT_EQ(area->items.size(), 1u);
}

TEST_F(GameplayActions, GrabCommitsAtTheSimulationBoundaryOnlyOnce)
{
    auto* target = item({3, 2, 0});
    states[0].pickup_ticks = 30;
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    prepare();
    complete();
    ASSERT_EQ(states[0].phase, ActionPhase::grabbing);
    EXPECT_EQ(states[0].elapsed_ticks, 0u);
    for (uint32_t tick = 1; tick < 30; ++tick) {
        prepare();
        EXPECT_EQ(motions[0].kind, ActionMovementKind::stop);
        complete(ActionMovementStatus::arrived);
        EXPECT_EQ(states[0].elapsed_ticks, tick);
        EXPECT_FALSE(results[0].changed);
        EXPECT_EQ(area->items.size(), 1u);
    }
    complete(ActionMovementStatus::arrived);
    ASSERT_EQ(results[0].status, ActionStatus::applied);
    EXPECT_TRUE(results[0].changed);
    EXPECT_TRUE(area->items.empty());
    EXPECT_EQ(states[0].phase, ActionPhase::recovering);
    complete();
    EXPECT_FALSE(results[0].changed);
    submit({.actor_index = 0, .kind = ActionKind::cancel});
    prepare();
    EXPECT_EQ(results[0].status, ActionStatus::cancelled);
    EXPECT_TRUE(nw::kernel::objects().get<nw::Creature>(states[0].actor)->inventory().has_item(target));
}

TEST_F(GameplayActions, ReplacingGrabBeforeCommitLeavesTheItemOnTheGround)
{
    auto* target = item({3, 2, 0});
    states[0].pickup_ticks = 2;
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    prepare();
    complete();
    complete(ActionMovementStatus::arrived);
    ASSERT_EQ(states[0].elapsed_ticks, 1u);
    submit({.actor_index = 0, .kind = ActionKind::move_to, .destination = {4, 2, 0}});
    prepare();
    complete(ActionMovementStatus::arrived);
    EXPECT_FALSE(results[0].changed);
    EXPECT_EQ(area->items.size(), 1u);
    EXPECT_EQ(states[0].elapsed_ticks, 0u);
    EXPECT_EQ(states[0].pickup_ticks, 2u);
}

TEST_F(GameplayActions, GrabRevalidatesTargetAtCommit)
{
    auto* target = item({3, 2, 0});
    states[0].pickup_ticks = 1;
    submit({.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()});
    prepare();
    complete();
    position(target->handle(), {8, 2, 0});
    prepare();
    complete(ActionMovementStatus::arrived);
    EXPECT_EQ(results[0].status, ActionStatus::rejected);
    EXPECT_EQ(results[0].transfer_status, nw::ItemTransferStatus::out_of_reach);
    EXPECT_EQ(area->items.size(), 1u);
}

TEST_F(GameplayActions, CompetingGrabsStillHaveOnlyOneOwner)
{
    auto* target = item({3, 2, 0});
    for (auto& state : states) {
        state.pickup_ticks = 2;
    }
    const std::array requests{
        ActorActionRequest{.actor_index = 0, .kind = ActionKind::pickup, .target = target->handle()},
        ActorActionRequest{.actor_index = 1, .kind = ActionKind::pickup, .target = target->handle()},
    };
    ASSERT_TRUE(submit_actor_actions(states, requests, results));
    prepare();
    complete();
    complete(ActionMovementStatus::arrived);
    // All unfinished progress survives a value copy, without animation state,
    // callbacks, or a rendering clock. This is not world serialization.
    const auto saved = states;
    states = {};
    states = saved;
    complete(ActionMovementStatus::arrived);
    EXPECT_EQ(results[0].status, ActionStatus::applied);
    EXPECT_EQ(results[1].status, ActionStatus::rejected);
    EXPECT_TRUE(nw::kernel::objects().get<nw::Creature>(states[0].actor)->inventory().has_item(target));
    EXPECT_FALSE(nw::kernel::objects().get<nw::Creature>(states[1].actor)->inventory().has_item(target));
}

} // namespace
