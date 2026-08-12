#include "smalls_creature_inventory.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <limits>
#include <span>

namespace nw::toolset {
namespace {

InventoryTextSlice append_text(std::string_view value, std::string& output)
{
    if (output.size() >= std::numeric_limits<uint32_t>::max()) {
        return {};
    }
    const size_t available = std::numeric_limits<uint32_t>::max() - output.size();
    const size_t length = std::min(value.size(), available);
    const auto offset = static_cast<uint32_t>(output.size());
    output.append(value.data(), length);
    return {offset, static_cast<uint32_t>(length)};
}

smalls::Value object_value(smalls::Runtime& runtime, ObjectHandle object)
{
    auto result = smalls::Value::make_object(object);
    result.type_id = runtime.object_subtype_for_tag(object.type);
    return result;
}

bool read_string_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    std::string_view field,
    std::string_view& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.string_type()
        || value.storage != smalls::ValueStorage::heap) {
        return false;
    }
    output = value.data.hptr.value == 0
        ? std::string_view{}
        : runtime.get_string_view(value.data.hptr);
    return true;
}

bool read_item_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    ObjectHandle& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, "item");
    if (!runtime.is_object_like_type(value.type_id)
        || value.data.oval.type != ObjectType::item) {
        return false;
    }
    output = value.data.oval;
    return true;
}

struct InventoryDisplayRow {
    ObjectHandle item{};
    InventoryTextSlice name;
    InventoryTextSlice resref;
    int32_t stack_size = 0;
};

bool copy_inventory_display_rows(smalls::Runtime& runtime,
    std::span<const ObjectHandle> expected_items,
    std::vector<InventoryDisplayRow>& output,
    std::string& text)
{
    const auto item_type = runtime.object_subtype_for_tag(ObjectType::item);
    const auto array_type = runtime.type_id("array!(Item)", false);
    if (item_type == smalls::invalid_type_id
        || array_type == smalls::invalid_type_id) {
        return false;
    }
    const auto items_ptr = runtime.create_array_typed(
        item_type, expected_items.size());
    auto* items = runtime.get_array_typed(items_ptr);
    if (!items) {
        return false;
    }
    for (const auto item : expected_items) {
        items->append_value(object_value(runtime, item), runtime);
    }

    const auto result = runtime.execute_script(
        "nwn1.item", "get_inventory_editor_rows",
        {smalls::Value::make_heap(items_ptr, array_type)});
    if (!result.ok() || result.value.storage != smalls::ValueStorage::heap
        || result.value.data.hptr.value == 0) {
        return false;
    }

    const auto* rows = runtime.get_array_typed(result.value.data.hptr);
    if (!rows || rows->size() != expected_items.size()) {
        return false;
    }

    output.clear();
    output.reserve(rows->size());
    for (size_t index = 0; index < rows->size(); ++index) {
        smalls::Value row;
        ObjectHandle item;
        std::string_view name;
        std::string_view resref;
        if (!rows->get_value(index, row, runtime)
            || !read_item_field(runtime, row, item)
            || item != expected_items[index]
            || !read_string_field(runtime, row, "name", name)
            || !read_string_field(runtime, row, "resref", resref)) {
            return false;
        }
        const auto stack = runtime.read_struct_field(
            row.data.hptr, row.type_id, "stack_size");
        if (stack.type_id != runtime.int_type() || stack.data.ival < 0) {
            return false;
        }
        output.push_back(InventoryDisplayRow{
            .item = item,
            .name = append_text(name, text),
            .resref = append_text(resref, text),
            .stack_size = stack.data.ival,
        });
    }
    return true;
}

} // namespace

std::string_view InventoryViewSnapshot::text_view(
    InventoryTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

void build_object_inventory_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ItemIconTextureCache& icon_cache,
    InventoryViewSnapshot& output)
{
    output = {};
    output.object = active_object;
    auto* creature = active_object.type == ObjectType::creature
        ? kernel::objects().get<Creature>(active_object)
        : nullptr;
    auto* item_owner = active_object.type == ObjectType::item
        ? kernel::objects().get<Item>(active_object)
        : nullptr;
    Inventory* owner_inventory = nullptr;
    if (active_object.type == ObjectType::creature && creature) {
        owner_inventory = &creature->inventory();
    } else if (active_object.type == ObjectType::item && item_owner) {
        owner_inventory = &item_owner->inventory();
    }
    if (!owner_inventory) {
        output.status = InventoryViewStatus::invalid_object;
        output.diagnostic = "Active object is not a live Creature or Item";
        return;
    }

    const auto& object_inventory = *owner_inventory;
    if (object_inventory.pages() <= 0
        || object_inventory.pages() > std::numeric_limits<uint8_t>::max()
        || object_inventory.rows() <= 0
        || object_inventory.rows() > Inventory::max_rows
        || object_inventory.columns() <= 0
        || object_inventory.columns() > Inventory::max_columns) {
        output.status = InventoryViewStatus::invalid_data;
        output.diagnostic = "Object inventory has invalid dimensions";
        return;
    }
    output.page_count = static_cast<uint8_t>(object_inventory.pages());
    output.row_count = static_cast<uint8_t>(object_inventory.rows());
    output.column_count = static_cast<uint8_t>(object_inventory.columns());

    std::vector<ObjectHandle> editor_items;
    editor_items.reserve(output.equipment.size() + object_inventory.items.size());
    if (creature) {
        for (const auto& equipped : creature->equipment.equips) {
            const auto* item = equip_item_ptr(equipped);
            if (!item) {
                if (!equipped.empty()) {
                    output.status = InventoryViewStatus::invalid_data;
                    output.diagnostic = "Creature equipment contains an uninstantiated item";
                    return;
                }
                continue;
            }
            editor_items.push_back(item->handle());
        }
    }
    for (const auto& entry : object_inventory.items) {
        const auto* item = inventory_item_ptr(entry);
        if (!item) {
            output.status = InventoryViewStatus::invalid_data;
            output.diagnostic = "Object inventory contains an uninstantiated item";
            return;
        }
        editor_items.push_back(item->handle());
    }

    std::vector<InventoryDisplayRow> display_rows;
    if (!copy_inventory_display_rows(
            runtime, editor_items, display_rows, output.text)) {
        output.status = InventoryViewStatus::invalid_data;
        output.diagnostic = "Smalls inventory editor rows do not match the live item batch";
        return;
    }
    size_t display_index = 0;

    struct IconTarget {
        uint32_t index = 0;
        bool equipment = false;
    };
    std::vector<ObjectHandle> icon_items;
    std::vector<IconTarget> icon_targets;
    icon_items.reserve(output.equipment.size() + object_inventory.items.size());
    icon_targets.reserve(output.equipment.size() + object_inventory.items.size());

    for (size_t index = 0; creature && index < output.equipment.size(); ++index) {
        auto& row = output.equipment[index];
        row.slot = static_cast<EquipIndex>(index);
        auto* item = equip_item_ptr(creature->equipment.equips[index]);
        if (!item) {
            if (!creature->equipment.equips[index].empty()) {
                output.status = InventoryViewStatus::invalid_data;
                output.diagnostic = "Creature equipment contains an uninstantiated item";
                return;
            }
            continue;
        }
        row.item = item->handle();
        const auto& display = display_rows[display_index++];
        row.name = display.name;
        row.resref = display.resref;
        row.stack_size = display.stack_size;
        icon_items.push_back(item->handle());
        icon_targets.push_back(IconTarget{
            .index = static_cast<uint32_t>(index),
            .equipment = true,
        });
    }

    const auto& inventory = object_inventory.items;
    output.inventory.reserve(inventory.size());
    for (size_t index = 0; index < inventory.size(); ++index) {
        const auto& entry = inventory[index];
        auto* item = inventory_item_ptr(entry);
        if (!item) {
            output.status = InventoryViewStatus::invalid_data;
            output.diagnostic = "Object inventory contains an uninstantiated item";
            return;
        }
        const auto* layout = kernel::objects().components().find_item_layout(item->handle());
        if (!layout || layout->inventory_width <= 0 || layout->inventory_height <= 0
            || layout->inventory_width > output.column_count
            || layout->inventory_height > output.row_count) {
            output.status = InventoryViewStatus::invalid_data;
            output.diagnostic = "Inventory item has an invalid native footprint";
            return;
        }
        const auto slot = object_inventory.xy_to_slot(entry.pos_x, entry.pos_y);
        if (slot.page < 0 || slot.page >= output.page_count
            || slot.row < layout->inventory_height - 1
            || slot.row >= output.row_count
            || slot.col < 0
            || slot.col + layout->inventory_width > output.column_count) {
            output.status = InventoryViewStatus::invalid_data;
            output.diagnostic = "Inventory item position or footprint is outside the inventory grid";
            return;
        }
        InventoryRow row{
            .item = item->handle(),
            .source_index = static_cast<uint32_t>(index),
            .page = static_cast<uint8_t>(slot.page),
            .row = static_cast<uint8_t>(slot.row),
            .column = static_cast<uint8_t>(slot.col),
            .width = static_cast<uint8_t>(layout->inventory_width),
            .height = static_cast<uint8_t>(layout->inventory_height),
            .infinite = entry.infinite,
        };
        const auto& display = display_rows[display_index++];
        row.name = display.name;
        row.resref = display.resref;
        row.stack_size = display.stack_size;
        output.inventory.push_back(row);
        icon_items.push_back(item->handle());
        icon_targets.push_back(IconTarget{
            .index = static_cast<uint32_t>(output.inventory.size() - 1),
            .equipment = false,
        });
    }

    const auto* visual = kernel::objects().components().find_visual(active_object);
    if (!visual || visual->body_variant < 0
        || visual->body_variant >= ObjectItemIconState::variant_count) {
        output.status = InventoryViewStatus::invalid_data;
        output.diagnostic = "Inventory owner has no valid materialized icon variant";
        return;
    }
    ItemIconBatch icon_batch;
    build_item_icon_images(static_cast<uint8_t>(visual->body_variant),
        icon_items, icon_cache, icon_batch);
    for (size_t index = 0; index < icon_batch.sources.size(); ++index) {
        if (icon_batch.sources[index].empty()) {
            continue;
        }
        const auto source = append_text(icon_batch.sources[index], output.text);
        const auto target = icon_targets[index];
        if (target.equipment) {
            auto& row = output.equipment[target.index];
            const auto* texture = find_generated_texture(
                icon_cache.textures, icon_batch.sources[index]);
            if (!texture || texture->visible_width == 0 || texture->visible_height == 0
                || static_cast<uint64_t>(texture->visible_x) + texture->visible_width > texture->width
                || static_cast<uint64_t>(texture->visible_y) + texture->visible_height > texture->height) {
                output.status = InventoryViewStatus::invalid_data;
                output.diagnostic = "Equipped item icon has invalid visible bounds";
                return;
            }
            row.icon_source = source;
            row.icon_visible_x = texture->visible_x;
            row.icon_visible_y = texture->visible_y;
            row.icon_visible_width = texture->visible_width;
            row.icon_visible_height = texture->visible_height;
        } else {
            output.inventory[target.index].icon_source = source;
        }
    }
    if (!icon_batch.protocol_valid) {
        output.diagnostic = icon_batch.diagnostic;
    }

    output.status = InventoryViewStatus::ready;
}

void build_creature_inventory_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ItemIconTextureCache& icon_cache,
    CreatureInventoryViewSnapshot& output)
{
    if (active_object.type != ObjectType::creature) {
        output = {};
        output.object = active_object;
        output.status = InventoryViewStatus::invalid_object;
        output.diagnostic = "Active object is not a live Creature";
        return;
    }
    build_object_inventory_rows(runtime, active_object, icon_cache, output);
}

void build_item_inventory_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ItemIconTextureCache& icon_cache,
    ItemInventoryViewSnapshot& output)
{
    if (active_object.type != ObjectType::item) {
        output = {};
        output.object = active_object;
        output.status = InventoryViewStatus::invalid_object;
        output.diagnostic = "Active object is not a live Item";
        return;
    }
    build_object_inventory_rows(runtime, active_object, icon_cache, output);
}

} // namespace nw::toolset
