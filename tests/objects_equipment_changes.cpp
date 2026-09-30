#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/equipment_changes.hpp>

namespace {

class EquipmentChanges : public ::testing::Test {
protected:
    nw::Creature* creature = nullptr;
    std::vector<nw::ObjectHandle> items;

    void SetUp() override
    {
        creature = nw::kernel::objects().make<nw::Creature>();
        ASSERT_NE(creature, nullptr);
        creature->inventory() = nw::Inventory{1, 2, 2, creature};
    }

    void TearDown() override
    {
        nw::kernel::objects().destroy(creature->handle());
        for (const auto item : items) {
            if (nw::kernel::objects().valid(item)) { nw::kernel::objects().destroy(item); }
        }
    }

    nw::Item* item(int width = 1, int height = 1)
    {
        auto* result = nw::kernel::objects().make<nw::Item>();
        if (!result) { return nullptr; }
        items.push_back(result->handle());
        EXPECT_TRUE(nw::kernel::objects().components().set_item_layout(result->handle(), width, height));
        return result;
    }

    std::vector<nw::InventoryItem> inventory() const
    {
        return {creature->inventory().items.begin(), creature->inventory().items.end()};
    }
};

TEST_F(EquipmentChanges, FullInventorySwapUsesFreedCellAndRestoresExactState)
{
    auto* old = item();
    auto* incoming = item();
    ASSERT_TRUE(nw::equip_item_in_slot(creature, old, nw::EquipIndex::belt));
    ASSERT_TRUE(creature->inventory().add_item(item()));
    ASSERT_TRUE(creature->inventory().add_item(incoming));
    creature->inventory().items.back().infinite = true;
    ASSERT_TRUE(creature->inventory().add_item(item()));
    ASSERT_TRUE(creature->inventory().add_item(item()));
    const auto before = inventory();
    const auto grid = creature->inventory().inventory_bitset;
    const auto revision = creature->equipment.equip_version;
    EXPECT_FALSE(creature->inventory().can_add_item(old));
    const std::array changes{nw::EquipmentChange{10, old->handle(), incoming->handle()}};
    const auto batch = nw::prepare_equipment_changes(creature->handle(), changes);
    ASSERT_TRUE(batch);
    EXPECT_EQ(inventory(), before);
    EXPECT_EQ(creature->equipment.equip_version, revision);
    ASSERT_TRUE(nw::apply_equipment_changes(*batch));
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), incoming);
    EXPECT_EQ(creature->equipment.equip_version, revision + 1);
    EXPECT_EQ(creature->inventory().items[1].item.as<nw::ObjectHandle>(), old->handle());
    EXPECT_EQ(creature->inventory().items[1].pos_x, before[1].pos_x);
    EXPECT_EQ(creature->inventory().items[1].pos_y, before[1].pos_y);
    EXPECT_EQ(creature->inventory().inventory_bitset, grid);
    const auto after = inventory();
    ASSERT_TRUE(nw::apply_equipment_changes(*batch, true));
    EXPECT_EQ(inventory(), before);
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), old);
    ASSERT_TRUE(nw::apply_equipment_changes(*batch));
    EXPECT_EQ(inventory(), after);
}

TEST_F(EquipmentChanges, CapacityAndInvalidRequestsRejectWithoutMutation)
{
    auto* old = item(2, 1);
    auto* incoming = item();
    auto* foreign = item();
    ASSERT_TRUE(nw::equip_item_in_slot(creature, old, nw::EquipIndex::belt));
    ASSERT_TRUE(creature->inventory().add_item(incoming));
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(creature->inventory().add_item(item()));
    }
    const auto before = inventory();
    const auto grid = creature->inventory().inventory_bitset;
    const auto version = creature->equipment.equip_version;
    for (const auto& changes : {
             std::vector{nw::EquipmentChange{10, old->handle(), incoming->handle()}},
             std::vector{nw::EquipmentChange{10, old->handle(), foreign->handle()}},
             std::vector{nw::EquipmentChange{18, old->handle(), incoming->handle()}},
             std::vector{nw::EquipmentChange{10, {}, incoming->handle()}},
             std::vector{nw::EquipmentChange{10, old->handle(), incoming->handle()}, nw::EquipmentChange{10, old->handle(), {}}}}) {
        EXPECT_FALSE(nw::prepare_equipment_changes(creature->handle(), changes));
        EXPECT_EQ(inventory(), before);
        EXPECT_EQ(creature->inventory().inventory_bitset, grid);
        EXPECT_EQ(creature->equipment.equip_version, version);
        EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), old);
    }
}

TEST_F(EquipmentChanges, MultiSlotExchangeAndDeferredStaleRequests)
{
    auto* right = item();
    auto* left = item();
    ASSERT_TRUE(nw::equip_item_in_slot(creature, right, nw::EquipIndex::righthand));
    ASSERT_TRUE(nw::equip_item_in_slot(creature, left, nw::EquipIndex::lefthand));
    const std::array changes{nw::EquipmentChange{4, right->handle(), left->handle()},
        nw::EquipmentChange{5, left->handle(), right->handle()}};
    const auto batch = nw::prepare_equipment_changes(creature->handle(), changes);
    ASSERT_TRUE(batch);
    ASSERT_TRUE(nw::apply_equipment_changes(*batch));
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::righthand), left);
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::lefthand), right);
    EXPECT_TRUE(creature->inventory().items.empty());
    EXPECT_FALSE(nw::apply_equipment_changes(*batch));
    ASSERT_TRUE(nw::apply_equipment_changes(*batch, true));
    ASSERT_TRUE(creature->inventory().add_item(item()));
    const auto before = inventory();
    EXPECT_FALSE(nw::apply_equipment_changes(*batch));
    EXPECT_EQ(inventory(), before);
    const auto refreshed = nw::prepare_equipment_changes(creature->handle(), changes);
    ASSERT_TRUE(refreshed);
    ASSERT_TRUE(nw::kernel::objects().components().set_item_layout(right->handle(), 2, 1));
    EXPECT_FALSE(nw::apply_equipment_changes(*refreshed));
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::righthand), right);
}

TEST_F(EquipmentChanges, FailedBatchDoesNotPublishFirstSlotOrRevision)
{
    auto* first = item();
    auto* second = item(2, 2);
    auto* incoming = item();
    ASSERT_TRUE(nw::equip_item_in_slot(creature, first, nw::EquipIndex::belt));
    ASSERT_TRUE(nw::equip_item_in_slot(creature, second, nw::EquipIndex::neck));
    ASSERT_TRUE(creature->inventory().add_item(incoming));
    ASSERT_TRUE(creature->inventory().add_item(item()));
    const auto version = creature->equipment.equip_version;
    const auto before = inventory();
    const std::array changes{nw::EquipmentChange{10, first->handle(), incoming->handle()}, nw::EquipmentChange{9, second->handle(), {}}};
    EXPECT_FALSE(nw::prepare_equipment_changes(creature->handle(), changes));
    EXPECT_EQ(inventory(), before);
    EXPECT_EQ(creature->equipment.equip_version, version);
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), first);
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::neck), second);
}

} // namespace
