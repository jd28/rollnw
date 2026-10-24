#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/nav/NavTileBuild.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/equipment_changes.hpp>
#include <nw/objects/item_transfers.hpp>

#include <array>
#include <limits>

namespace {

class ItemTransfers : public ::testing::Test {
protected:
    nw::Area* area = nullptr;
    nw::Creature* actor = nullptr;
    nw::nav::NavAreaBuildSource source; // Navigation borrows the build source.
    nw::nav::NavWorldState navigation;
    std::vector<nw::ObjectHandle> created;

    void SetUp() override
    {
        area = nw::kernel::objects().make<nw::Area>();
        actor = nw::kernel::objects().make<nw::Creature>();
        ASSERT_NE(area, nullptr);
        ASSERT_NE(actor, nullptr);
        area->creatures.push_back(actor);
        actor->inventory() = nw::Inventory{1, 2, 2, actor};
        auto& components = nw::kernel::objects().components();
        ASSERT_TRUE(components.set_area(actor->handle(), area->handle().id));
        ASSERT_TRUE(components.set_position(actor->handle(), {4.0f, 4.0f, 0.0f}));
        source.width = source.height = 1;
        source.surface_walkable = {0u, 1u};
        source.surface_vertices = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {10, 10, 0}};
        source.surface_indices = {0, 2, 1, 1, 2, 3};
        source.surface_ids = {1, 1};
        nw::nav::NavTiledWorldBuildStats stats;
        nw::nav::NavTileBuildConfig config;
        config.erosion_cells = 1;
        ASSERT_EQ(nw::nav::build_tiled_nav_world(source, {}, config, navigation, stats), nw::nav::NavStatus::ok);
    }

    void TearDown() override
    {
        area->clear();
        nw::kernel::objects().destroy(area->handle());
        for (const auto item : created) {
            if (nw::kernel::objects().valid(item)) { nw::kernel::objects().destroy(item); }
        }
    }

    nw::Item* item(glm::vec3 position = {5, 4, 0})
    {
        auto* value = nw::kernel::objects().make<nw::Item>();
        created.push_back(value->handle());
        auto& components = nw::kernel::objects().components();
        EXPECT_TRUE(components.set_item_layout(value->handle(), 1, 1));
        EXPECT_TRUE(components.set_area(value->handle(), area->handle().id));
        EXPECT_TRUE(components.set_position(value->handle(), position));
        area->items.push_back(value);
        return value;
    }

    nw::ItemTransferResult transfer(std::span<const nw::ItemTransfer> rows)
    {
        return nw::transfer_area_items(area->handle(), actor->handle(), navigation, 2.0f, rows);
    }
};

TEST_F(ItemTransfers, PickupDropAndPickupPreserveIdentityAndOwnership)
{
    auto* value = item();
    const auto handle = value->handle();
    const std::array pickup{nw::ItemTransfer{handle, nw::ItemTransferDirection::pickup}};
    const std::array drop{nw::ItemTransfer{handle, nw::ItemTransferDirection::drop}};
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(transfer(pickup).ok());
        ASSERT_TRUE(area->items.empty());
        ASSERT_TRUE(actor->inventory().has_item(value));
        EXPECT_EQ(nw::kernel::objects().components().find_spatial(handle)->area, nw::object_invalid);
        ASSERT_TRUE(transfer(drop).ok());
        ASSERT_EQ(area->items.size(), 1u);
        EXPECT_EQ(area->items[0]->handle(), handle);
        EXPECT_EQ(actor->inventory().size(), 0u);
        EXPECT_TRUE(actor->inventory().inventory_bitset[0].none());
        const auto* spatial = nw::kernel::objects().components().find_spatial(handle);
        EXPECT_EQ(spatial->area, area->handle().id);
        EXPECT_EQ(spatial->position, glm::vec3(4, 4, 0));
    }
}

TEST_F(ItemTransfers, InvalidSecondRowAndDuplicateRowsRejectEntireBatch)
{
    auto* value = item();
    const auto spatial = *nw::kernel::objects().components().find_spatial(value->handle());
    const std::array mixed{nw::ItemTransfer{value->handle()}, nw::ItemTransfer{}};
    EXPECT_FALSE(transfer(mixed).ok());
    const std::array duplicate{nw::ItemTransfer{value->handle()}, nw::ItemTransfer{value->handle()}};
    EXPECT_FALSE(transfer(duplicate).ok());
    EXPECT_EQ(actor->inventory().size(), 0u);
    ASSERT_EQ(area->items.size(), 1u);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(value->handle())->position, spatial.position);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(value->handle())->area, spatial.area);
}

TEST_F(ItemTransfers, InsufficientCapacityRejectsWholeBatch)
{
    actor->inventory() = nw::Inventory{1, 1, 1, actor};
    auto* first = item();
    auto* second = item();
    const std::array rows{nw::ItemTransfer{first->handle()}, nw::ItemTransfer{second->handle()}};
    EXPECT_EQ(transfer(rows).status, nw::ItemTransferStatus::no_space);
    EXPECT_EQ(actor->inventory().size(), 0u);
    EXPECT_TRUE(actor->inventory().inventory_bitset[0].none());
    ASSERT_EQ(area->items.size(), 2u);
    EXPECT_EQ(area->items[0], first);
    EXPECT_EQ(area->items[1], second);
}

TEST_F(ItemTransfers, RejectsFarOffMeshAndForeignItems)
{
    auto* far = item({8, 4, 0});
    const std::array far_rows{nw::ItemTransfer{far->handle()}};
    EXPECT_EQ(transfer(far_rows).status, nw::ItemTransferStatus::out_of_reach);
    auto* raised = item({4, 4, 1});
    const std::array raised_rows{nw::ItemTransfer{raised->handle()}};
    EXPECT_EQ(transfer(raised_rows).status, nw::ItemTransferStatus::blocked);
    auto* foreign = item();
    area->items.pop_back();
    const std::array foreign_rows{nw::ItemTransfer{foreign->handle()}};
    EXPECT_EQ(transfer(foreign_rows).status, nw::ItemTransferStatus::invalid_ownership);
    EXPECT_EQ(actor->inventory().size(), 0u);
    EXPECT_EQ(area->items.size(), 2u);
}

TEST_F(ItemTransfers, EquippedItemsMustBeUnequippedBeforeDrop)
{
    auto* value = item();
    const std::array pickup{nw::ItemTransfer{value->handle()}};
    ASSERT_TRUE(transfer(pickup).ok());
    const std::array equip{nw::EquipmentChange{static_cast<int32_t>(nw::EquipIndex::belt), {}, value->handle()}};
    const auto batch = nw::prepare_equipment_changes(actor->handle(), equip);
    ASSERT_TRUE(batch);
    ASSERT_TRUE(nw::apply_equipment_changes(*batch));
    const std::array drop{nw::ItemTransfer{value->handle(), nw::ItemTransferDirection::drop}};
    EXPECT_EQ(transfer(drop).status, nw::ItemTransferStatus::invalid_ownership);
    EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), value);
    EXPECT_TRUE(area->items.empty());
    ASSERT_TRUE(nw::apply_equipment_changes(*batch, true));
    ASSERT_TRUE(transfer(drop).ok());
}

TEST_F(ItemTransfers, RejectsMalformedInventoryAndInvalidReach)
{
    auto* value = item();
    const std::array rows{nw::ItemTransfer{value->handle()}};
    EXPECT_EQ(nw::transfer_area_items(area->handle(), actor->handle(), navigation,
                  std::numeric_limits<float>::quiet_NaN(), rows)
                  .status,
        nw::ItemTransferStatus::invalid_input);
    actor->inventory().inventory_bitset[0].set(0);
    EXPECT_EQ(transfer(rows).status, nw::ItemTransferStatus::invalid_inventory);
    EXPECT_EQ(actor->inventory().size(), 0u);
    EXPECT_EQ(area->items.size(), 1u);
    EXPECT_TRUE(actor->inventory().inventory_bitset[0].test(0));
}

} // namespace
