#include <gtest/gtest.h>

#include "../tools/ui/rml_generated_texture.hpp"
#include "../tools/ui/smalls_creature_inventory.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>

namespace nwk = nw::kernel;

TEST(ClientSmallsCreatureInventory, BuildsFixedEquipmentAndVariableInventoryRows)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(creature->inventory().add_item(item));

    nw::toolset::CreatureInventoryViewSnapshot snapshot;
    nw::toolset::ItemIconTextureCache icon_cache;
    nw::toolset::build_creature_inventory_rows(
        nwk::runtime(), creature->handle(), icon_cache, snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::CreatureInventoryViewStatus::ready)
        << snapshot.diagnostic;
    EXPECT_EQ(snapshot.object, creature->handle());
    EXPECT_EQ(snapshot.page_count, creature->inventory().pages());
    EXPECT_EQ(snapshot.row_count, creature->inventory().rows());
    EXPECT_EQ(snapshot.column_count, creature->inventory().columns());
    EXPECT_EQ(snapshot.equipment.size(), 18);
    EXPECT_EQ(snapshot.inventory.size(), creature->inventory().items.size());

    size_t assigned = 0;
    for (size_t index = 0; index < snapshot.equipment.size(); ++index) {
        const auto& row = snapshot.equipment[index];
        EXPECT_EQ(static_cast<size_t>(row.slot), index);
        if (!row.assigned()) {
            continue;
        }
        ++assigned;
        EXPECT_FALSE(snapshot.text_view(row.name).empty());
        EXPECT_FALSE(snapshot.text_view(row.resref).empty());
        EXPECT_GE(row.stack_size, 0);
    }
    EXPECT_GT(assigned, 0);

    const auto& inventory_row = snapshot.inventory.back();
    const auto& inventory_entry = creature->inventory().items.back();
    const auto inventory_slot = creature->inventory().xy_to_slot(
        inventory_entry.pos_x, inventory_entry.pos_y);
    const auto* layout = nwk::objects().components().find_item_layout(item->handle());
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(inventory_row.item, item->handle());
    EXPECT_EQ(inventory_row.source_index, snapshot.inventory.size() - 1);
    EXPECT_EQ(inventory_row.page, inventory_slot.page);
    EXPECT_EQ(inventory_row.row, inventory_slot.row);
    EXPECT_EQ(inventory_row.column, inventory_slot.col);
    EXPECT_EQ(inventory_row.width, layout->inventory_width);
    EXPECT_EQ(inventory_row.height, layout->inventory_height);
    EXPECT_FALSE(snapshot.text_view(inventory_row.name).empty());
    EXPECT_EQ(snapshot.text_view(inventory_row.resref), item->resref.view());
    const auto icon_source = snapshot.text_view(inventory_row.icon_source);
    ASSERT_FALSE(icon_source.empty());
    EXPECT_NE(nw::toolset::find_generated_texture(icon_cache.textures, icon_source), nullptr);
    EXPECT_GE(inventory_row.stack_size, 0);
    ASSERT_FALSE(icon_cache.textures.empty());
    bool found_transparent_margin = false;
    for (const auto& texture : icon_cache.textures) {
        EXPECT_FALSE(texture.source.empty());
        EXPECT_GT(texture.width, 0);
        EXPECT_GT(texture.height, 0);
        EXPECT_GT(texture.visible_width, 0);
        EXPECT_GT(texture.visible_height, 0);
        EXPECT_LE(static_cast<uint64_t>(texture.visible_x) + texture.visible_width,
            texture.width);
        EXPECT_LE(static_cast<uint64_t>(texture.visible_y) + texture.visible_height,
            texture.height);
        found_transparent_margin |= texture.visible_x > 0 || texture.visible_y > 0
            || texture.visible_width < texture.width || texture.visible_height < texture.height;
        EXPECT_EQ(texture.rgba.size(),
            static_cast<size_t>(texture.width) * texture.height * 4);
    }
    EXPECT_TRUE(found_transparent_margin);
}

TEST(ClientSmallsCreatureInventory, RejectsFootprintsOutsideTheFixedPageGrid)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(creature->inventory().add_item(item));

    const auto* layout = nwk::objects().components().find_item_layout(item->handle());
    ASSERT_NE(layout, nullptr);
    const int32_t original_width = layout->inventory_width;
    const int32_t original_height = layout->inventory_height;
    ASSERT_TRUE(nwk::objects().components().set_item_layout(item->handle(), 11, 1));

    nw::toolset::CreatureInventoryViewSnapshot snapshot;
    nw::toolset::ItemIconTextureCache icon_cache;
    nw::toolset::build_creature_inventory_rows(
        nwk::runtime(), creature->handle(), icon_cache, snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::CreatureInventoryViewStatus::invalid_data);
    EXPECT_FALSE(snapshot.diagnostic.empty());

    EXPECT_TRUE(nwk::objects().components().set_item_layout(
        item->handle(), original_width, original_height));
}

TEST(ClientSmallsCreatureInventory, RejectsInvalidActiveObject)
{
    nw::toolset::CreatureInventoryViewSnapshot snapshot;
    nw::toolset::ItemIconTextureCache icon_cache;
    nw::toolset::build_creature_inventory_rows(
        nwk::runtime(), nw::ObjectHandle{}, icon_cache, snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::CreatureInventoryViewStatus::invalid_object);
    EXPECT_FALSE(snapshot.diagnostic.empty());
}

TEST(ClientSmallsCreatureInventory, BuildsOnePageItemInventoryWithSharedRows)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* owner = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    auto* item = nwk::objects().load<nw::Item>("nw_wswss001");
    ASSERT_NE(owner, nullptr);
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(owner->inventory().add_item(item));

    nw::toolset::ItemInventoryViewSnapshot snapshot;
    nw::toolset::ItemIconTextureCache icon_cache;
    nw::toolset::build_item_inventory_rows(
        nwk::runtime(), owner->handle(), icon_cache, snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::InventoryViewStatus::ready)
        << snapshot.diagnostic;
    EXPECT_EQ(snapshot.object, owner->handle());
    EXPECT_EQ(snapshot.page_count, 1);
    EXPECT_EQ(snapshot.row_count, 6);
    EXPECT_EQ(snapshot.column_count, 10);
    EXPECT_EQ(snapshot.inventory.size(), 1);
    EXPECT_TRUE(std::none_of(snapshot.equipment.begin(), snapshot.equipment.end(),
        [](const auto& row) { return row.assigned(); }));
    EXPECT_EQ(snapshot.inventory.front().item, item->handle());
    EXPECT_FALSE(snapshot.text_view(snapshot.inventory.front().icon_source).empty());
}
