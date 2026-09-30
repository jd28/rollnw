#include "equipment_changes.hpp"

#include "Creature.hpp"
#include "Item.hpp"
#include "ObjectComponentSystem.hpp"
#include "ObjectManager.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_set>

namespace nw {
namespace {

bool valid_geometry(const Inventory& inventory)
{
    return inventory.pages() > 0 && inventory.pages() <= UINT8_MAX
        && inventory.inventory_bitset.size() == static_cast<size_t>(inventory.pages())
        && inventory.rows() > 0 && inventory.rows() <= Inventory::max_rows
        && inventory.columns() > 0 && inventory.columns() <= Inventory::max_columns
        && inventory.items.size() <= 1024;
}

bool valid_item(ObjectHandle item)
{
    return item.type == ObjectType::item && kernel::objects().valid(item);
}

bool empty_item(ObjectHandle item) { return item == ObjectHandle{}; }

const ObjectItemLayoutState* footprint(ObjectHandle item, const Inventory& inventory)
{
    const auto* layout = kernel::objects().components().find_item_layout(item);
    return valid_item(item) && layout && layout->inventory_width > 0
            && layout->inventory_width <= inventory.columns() && layout->inventory_height > 0
            && layout->inventory_height <= inventory.rows()
        ? layout
        : nullptr;
}

bool insert_entry(Inventory& inventory, const InventoryItem& entry)
{
    if (!entry.item.is<ObjectHandle>()) { return false; }
    const auto* layout = footprint(entry.item.as<ObjectHandle>(), inventory);
    const auto cell = inventory.xy_to_slot(entry.pos_x, entry.pos_y);
    if (!layout || cell.page < 0 || cell.page >= inventory.pages()
        || cell.row < 0 || cell.row >= inventory.rows() || cell.col < 0 || cell.col >= inventory.columns()
        || !inventory.check_available(cell.page, cell.row, cell.col, layout->inventory_width, layout->inventory_height)) {
        return false;
    }
    return inventory.insert_item(cell.page, cell.row, cell.col, layout->inventory_width, layout->inventory_height);
}

EquipmentState snapshot(const Creature& creature)
{
    return {creature.equipment.equips,
        {creature.inventory().items.begin(), creature.inventory().items.end()},
        creature.inventory().inventory_bitset};
}

bool state_matches(const Creature& creature, const EquipmentState& state)
{
    const auto& inventory = creature.inventory();
    return creature.equipment.equips == state.equipment
        && std::ranges::equal(inventory.items, state.inventory)
        && inventory.inventory_bitset == state.occupancy;
}

bool contains_equipped(const EquipmentState& state, ObjectHandle item)
{
    return std::ranges::any_of(state.equipment, [item](const auto& equipped) {
        return equipped.template is<ObjectHandle>() && equipped.template as<ObjectHandle>() == item;
    });
}

} // namespace

std::optional<EquipmentChangeBatch> prepare_equipment_changes(
    ObjectHandle creature_handle, std::span<const EquipmentChange> changes)
{
    const auto* creature = kernel::objects().get<Creature>(creature_handle);
    if (!creature || changes.size() > 18 || !valid_geometry(creature->inventory())) { return std::nullopt; }
    const auto& inventory = creature->inventory();
    EquipmentChangeBatch result;
    result.creature = creature_handle;
    result.service_generation = kernel::services().generation();
    result.rows = inventory.rows();
    result.columns = inventory.columns();
    result.before = snapshot(*creature);
    result.after.equipment = result.before.equipment;
    std::array<bool, 18> slots{};
    for (const auto& change : changes) {
        if (change.slot < 0 || change.slot >= 18 || slots[change.slot]
            || (!empty_item(change.before) && !valid_item(change.before))
            || (!empty_item(change.after) && !valid_item(change.after))) { return std::nullopt; }
        slots[change.slot] = true;
        const auto& current = creature->equipment.equips[change.slot];
        if (empty_item(change.before) ? !current.empty()
                                      : !current.is<ObjectHandle>() || current.as<ObjectHandle>() != change.before) {
            return std::nullopt;
        }
        if (change.before == change.after) { continue; }
        result.changes.push_back(change);
        result.after.equipment[change.slot] = empty_item(change.after) ? EquipItem{} : EquipItem{change.after};
    }
    if (result.changes.empty()) {
        result.after = result.before;
        return result;
    }

    // Validate existing ownership before constructing the final layout. Scoped
    // object borrows resolve native layouts; only handles and indices are kept.
    std::unordered_set<uint64_t> owned;
    owned.reserve(inventory.items.size() + 18);
    for (const auto& equipped : result.before.equipment) {
        if (equipped.empty()) { continue; }
        if (!equipped.is<ObjectHandle>() || !valid_item(equipped.as<ObjectHandle>())
            || !owned.insert(equipped.as<ObjectHandle>().to_ull()).second) { return std::nullopt; }
    }
    Inventory scratch(inventory.pages(), inventory.rows(), inventory.columns());
    for (const auto& entry : inventory.items) {
        if (!entry.item.is<ObjectHandle>() || !owned.insert(entry.item.as<ObjectHandle>().to_ull()).second
            || !insert_entry(scratch, entry)) { return std::nullopt; }
        const auto item = entry.item.as<ObjectHandle>();
        const auto* layout = footprint(item, inventory);
        if (!layout) { return std::nullopt; }
        result.footprints.push_back({item, layout->inventory_width, layout->inventory_height});
    }
    if (scratch.inventory_bitset != inventory.inventory_bitset) { return std::nullopt; }
    for (const auto& change : result.changes) {
        if (!empty_item(change.after) && !owned.contains(change.after.to_ull())) { return std::nullopt; }
        if (!empty_item(change.before)) {
            const auto* layout = footprint(change.before, inventory);
            if (!layout) { return std::nullopt; }
            result.footprints.push_back({change.before, layout->inventory_width, layout->inventory_height});
        }
    }
    std::unordered_set<uint64_t> final_equipped;
    for (const auto& equipped : result.after.equipment) {
        if (!equipped.empty() && !final_equipped.insert(equipped.as<ObjectHandle>().to_ull()).second) { return std::nullopt; }
    }
    std::fill(scratch.inventory_bitset.begin(), scratch.inventory_bitset.end(), Inventory::Storage{});
    for (const auto& entry : inventory.items) {
        if (final_equipped.contains(entry.item.as<ObjectHandle>().to_ull())) { continue; }
        if (!insert_entry(scratch, entry)) { return std::nullopt; }
        scratch.items.push_back(entry);
    }
    for (const auto& change : result.changes) {
        if (empty_item(change.before) || contains_equipped(result.after, change.before)) { continue; }
        const auto* layout = footprint(change.before, inventory);
        if (!layout || scratch.items.size() >= scratch.items.capacity()) { return std::nullopt; }
        const auto source = std::ranges::find_if(inventory.items, [&](const auto& entry) {
            return entry.item.template as<ObjectHandle>() == change.after;
        });
        InventorySlot cell;
        if (source != inventory.items.end()) {
            const auto preferred = inventory.xy_to_slot(source->pos_x, source->pos_y);
            if (scratch.check_available(preferred.page, preferred.row, preferred.col,
                    layout->inventory_width, layout->inventory_height)) { cell = preferred; }
        }
        if (cell.page < 0) { cell = scratch.find_slot(layout->inventory_width, layout->inventory_height); }
        if (cell.page < 0) { return std::nullopt; }
        const auto [x, y] = scratch.slot_to_xy(cell);
        InventoryItem entry{.pos_x = x, .pos_y = y, .item = change.before};
        if (!insert_entry(scratch, entry)) { return std::nullopt; }
        const auto index = source == inventory.items.end() ? scratch.items.size()
                                                           : std::min(static_cast<size_t>(source - inventory.items.begin()), scratch.items.size());
        scratch.items.insert(scratch.items.begin() + index, entry);
    }
    result.after.inventory.assign(scratch.items.begin(), scratch.items.end());
    result.after.occupancy = std::move(scratch.inventory_bitset);
    return result;
}

bool apply_equipment_changes(const EquipmentChangeBatch& batch, bool reverse)
{
    if (batch.service_generation != kernel::services().generation()) { return false; }
    auto* creature = kernel::objects().get<Creature>(batch.creature);
    if (!creature || !valid_geometry(creature->inventory())) { return false; }
    auto& inventory = creature->inventory();
    const auto& expected = reverse ? batch.after : batch.before;
    const auto& replacement = reverse ? batch.before : batch.after;
    if (inventory.rows() != batch.rows || inventory.columns() != batch.columns
        || !state_matches(*creature, expected) || replacement.inventory.size() > inventory.items.capacity()
        || replacement.occupancy.size() != inventory.inventory_bitset.size()) { return false; }
    for (const auto& captured : batch.footprints) {
        const auto* layout = footprint(captured.item, inventory);
        if (!layout || layout->inventory_width != captured.width || layout->inventory_height != captured.height) { return false; }
    }
    for (const auto& equipped : replacement.equipment) {
        if (!equipped.empty() && (!equipped.is<ObjectHandle>() || !valid_item(equipped.as<ObjectHandle>()))) { return false; }
    }
    if (batch.changes.empty()) { return true; }
    static_assert(std::is_nothrow_copy_constructible_v<InventoryItem>);
    static_assert(std::is_nothrow_copy_assignable_v<EquipItem>);
    inventory.items.clear();
    for (const auto& entry : replacement.inventory) {
        inventory.items.push_back(entry);
    }
    std::copy(replacement.occupancy.begin(), replacement.occupancy.end(), inventory.inventory_bitset.begin());
    creature->equipment.equips = replacement.equipment;
    ++creature->equipment.equip_version;
    return true;
}

} // namespace nw
