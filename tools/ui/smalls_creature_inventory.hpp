#pragma once

#include "rml_generated_texture.hpp"
#include "smalls_item_icons.hpp"

#include <nw/objects/Equips.hpp>
#include <nw/objects/ObjectHandle.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

struct InventoryTextSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct EquipmentRow {
    EquipIndex slot = EquipIndex::invalid;
    ObjectHandle item{};
    InventoryTextSlice name;
    InventoryTextSlice resref;
    InventoryTextSlice icon_source;
    uint32_t icon_visible_x = 0;
    uint32_t icon_visible_y = 0;
    uint32_t icon_visible_width = 0;
    uint32_t icon_visible_height = 0;
    int32_t stack_size = 0;

    [[nodiscard]] bool assigned() const noexcept
    {
        return item.type == ObjectType::item;
    }
};

struct InventoryRow {
    ObjectHandle item{};
    uint32_t source_index = 0;
    uint8_t page = 0;
    uint8_t row = 0;
    uint8_t column = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    InventoryTextSlice name;
    InventoryTextSlice resref;
    InventoryTextSlice icon_source;
    int32_t stack_size = 0;
    bool infinite = false;
};

enum class InventoryViewStatus : uint8_t {
    empty,
    ready,
    invalid_object,
    invalid_data,
};

// Cold display protocol for one active Creature, Item, or Placeable. Equipment is populated
// only for Creature owners; inventory is contiguous source order with validated
// page coordinates and footprints. Text slices remain valid until rebuild.
struct InventoryViewSnapshot {
    ObjectHandle object{};
    InventoryViewStatus status = InventoryViewStatus::empty;
    uint8_t page_count = 0;
    uint8_t row_count = 0;
    uint8_t column_count = 0;
    std::array<EquipmentRow, 18> equipment;
    std::vector<InventoryRow> inventory;
    std::string text;
    std::string diagnostic;

    [[nodiscard]] std::string_view text_view(InventoryTextSlice slice) const noexcept;
};

using CreatureInventoryTextSlice = InventoryTextSlice;
using CreatureEquipmentRow = EquipmentRow;
using CreatureInventoryRow = InventoryRow;
using CreatureInventoryViewStatus = InventoryViewStatus;
using CreatureInventoryViewSnapshot = InventoryViewSnapshot;
using ItemInventoryViewSnapshot = InventoryViewSnapshot;

void build_object_inventory_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ItemIconTextureCache& icon_cache,
    InventoryViewSnapshot& output);

void build_creature_inventory_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ItemIconTextureCache& icon_cache,
    CreatureInventoryViewSnapshot& output);

void build_item_inventory_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ItemIconTextureCache& icon_cache,
    ItemInventoryViewSnapshot& output);

} // namespace nw::toolset
