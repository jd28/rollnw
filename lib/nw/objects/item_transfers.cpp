#include "item_transfers.hpp"

#include "../nav/NavWorld.hpp"
#include "Area.hpp"
#include "Creature.hpp"
#include "Item.hpp"
#include "ObjectComponentSystem.hpp"
#include "ObjectManager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace nw {
namespace {

bool valid_layout(const ObjectItemLayoutState* layout, const Inventory& inventory)
{
    return layout && layout->inventory_width > 0 && layout->inventory_height > 0
        && layout->inventory_width <= inventory.columns()
        && layout->inventory_height <= inventory.rows();
}

bool finite(const glm::vec3& position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

} // namespace

ItemTransferResult transfer_area_items(ObjectHandle area_handle, ObjectHandle actor_handle,
    const nav::NavWorldState& navigation, float pickup_reach,
    std::span<const ItemTransfer> transfers)
{
    if (transfers.empty()) { return {ItemTransferStatus::success}; }
    if (transfers.size() > 1024 || !std::isfinite(pickup_reach) || pickup_reach <= 0.0f) { return {}; }
    auto& objects = kernel::objects();
    auto& components = objects.components();
    auto* area = area_handle.type == ObjectType::area ? objects.get<Area>(area_handle) : nullptr;
    auto* actor = actor_handle.type == ObjectType::creature ? objects.get<Creature>(actor_handle) : nullptr;
    const auto* actor_spatial = components.find_spatial(actor_handle);
    if (!area || !actor || !actor_spatial || actor_spatial->area != area_handle.id
        || !finite(actor_spatial->position)
        || std::count(area->creatures.begin(), area->creatures.end(), actor) != 1) {
        return {ItemTransferStatus::invalid_ownership};
    }
    const glm::vec3 actor_position = actor_spatial->position;
    auto& inventory = actor->inventory();
    if (inventory.owner != actor || inventory.pages() <= 0 || inventory.pages() > UINT8_MAX
        || inventory.rows() <= 0 || inventory.rows() > Inventory::max_rows
        || inventory.columns() <= 0 || inventory.columns() > Inventory::max_columns
        || inventory.inventory_bitset.size() != static_cast<size_t>(inventory.pages())
        || inventory.items.size() > 1024 || inventory.growable_) {
        return {ItemTransferStatus::invalid_inventory};
    }

    // Existing Area rows contain pointers, and Inventory rows contain kernel
    // handles. Resolve them once at this owner boundary; no new registry/lifetime.
    Vector<ObjectHandle> owned;
    owned.reserve(area->items.size() + inventory.items.size() + actor->equipment.equips.size());
    Inventory staged{inventory.pages(), inventory.rows(), inventory.columns(), actor};
    for (const auto& entry : inventory.items) {
        auto* item = inventory_item_ptr(entry);
        const auto* layout = item ? components.find_item_layout(item->handle()) : nullptr;
        if (!item || !valid_layout(layout, inventory)) { return {ItemTransferStatus::invalid_inventory}; }
        const auto slot = inventory.xy_to_slot(entry.pos_x, entry.pos_y);
        if (!staged.insert_item(slot.page, slot.row, slot.col, layout->inventory_width, layout->inventory_height)) {
            return {ItemTransferStatus::invalid_inventory};
        }
        staged.items.push_back(entry);
        owned.push_back(item->handle());
    }
    if (staged.inventory_bitset != inventory.inventory_bitset) { return {ItemTransferStatus::invalid_inventory}; }
    for (const auto& equipped : actor->equipment.equips) {
        if (equipped.empty()) { continue; }
        if (!equipped.is<ObjectHandle>() || equipped.as<ObjectHandle>().type != ObjectType::item
            || !objects.valid(equipped.as<ObjectHandle>())) { return {ItemTransferStatus::invalid_ownership}; }
        owned.push_back(equipped.as<ObjectHandle>());
    }
    for (const auto* item : area->items) {
        if (!item || !objects.valid(item->handle())) { return {ItemTransferStatus::invalid_ownership}; }
        const auto* spatial = components.find_spatial(item->handle());
        if (!spatial || spatial->area != area_handle.id || !finite(spatial->position)) {
            return {ItemTransferStatus::invalid_ownership};
        }
        owned.push_back(item->handle());
    }
    std::sort(owned.begin(), owned.end());
    if (std::adjacent_find(owned.begin(), owned.end()) != owned.end()) { return {ItemTransferStatus::invalid_ownership}; }

    Vector<ObjectHandle> requested;
    requested.reserve(transfers.size());
    for (const auto& transfer : transfers) {
        if (transfer.item.type != ObjectType::item || !objects.valid(transfer.item)
            || (transfer.direction != ItemTransferDirection::pickup && transfer.direction != ItemTransferDirection::drop)) { return {}; }
        requested.push_back(transfer.item);
    }
    std::sort(requested.begin(), requested.end());
    if (std::adjacent_find(requested.begin(), requested.end()) != requested.end()) { return {}; }

    const std::array positions{actor_position};
    std::array<nav::NavStatus, 1> admitted{};
    nav::validate_nav_positions(navigation, positions, admitted);
    if (admitted[0] != nav::NavStatus::ok) { return {ItemTransferStatus::blocked}; }

    Vector<Item*> ground = area->items;
    ground.reserve(ground.size() + transfers.size());
    Vector<ObjectSpatialState*> spatial_rows;
    spatial_rows.reserve(transfers.size());
    Vector<glm::vec3> corners;
    nav::NavRouteArena routes;
    if (std::ranges::any_of(transfers, [](const auto& row) { return row.direction == ItemTransferDirection::pickup; })) {
        corners.reserve(nav::maximum_nav_path_corners);
        routes.polygons.reserve(nav::maximum_nav_path_polygons);
        routes.tile_keys.reserve(nav::maximum_nav_path_polygons);
        routes.traversals.reserve(nav::maximum_nav_path_corners);
    }
    for (const auto& transfer : transfers) {
        auto* item = objects.get<Item>(transfer.item);
        auto* spatial = components.find_spatial(transfer.item);
        if (!spatial || !valid_layout(components.find_item_layout(transfer.item), inventory)) {
            return {ItemTransferStatus::invalid_inventory};
        }
        spatial_rows.push_back(spatial);
        const auto found = std::find(ground.begin(), ground.end(), item);
        if (transfer.direction == ItemTransferDirection::pickup) {
            if (found == ground.end()) { return {ItemTransferStatus::invalid_ownership}; }
            if (glm::distance(actor_position, spatial->position) > pickup_reach) { return {ItemTransferStatus::out_of_reach}; }
            const std::array item_positions{spatial->position};
            nav::validate_nav_positions(navigation, item_positions, admitted);
            if (admitted[0] != nav::NavStatus::ok) { return {ItemTransferStatus::blocked}; }
            const std::array paths{nav::NavPathRequest{actor_position, spatial->position}};
            std::array<nav::NavPathResult, 1> path_results{};
            nav::find_nav_paths(navigation, paths, corners, routes, path_results);
            const auto& path = path_results[0];
            if (path.status != nav::NavStatus::ok || path.traversal_count != 0 || path.corner_count == 0) {
                return {ItemTransferStatus::blocked};
            }
            float distance = 0.0f;
            glm::vec3 previous = actor_position;
            for (uint32_t i = 0; i < path.corner_count; ++i) {
                const auto& next = corners[path.corner_offset + i];
                distance += glm::distance(previous, next);
                previous = next;
            }
            distance += glm::distance(previous, spatial->position);
            if (distance > pickup_reach) { return {ItemTransferStatus::out_of_reach}; }
            if (staged.items.size() == staged.items.capacity() || !staged.add_item(item)) { return {ItemTransferStatus::no_space}; }
            ground.erase(found);
        } else {
            if (found != ground.end() || !staged.remove_item(item)) { return {ItemTransferStatus::invalid_ownership}; }
            ground.push_back(item);
        }
    }

    // All allocations and fallible work are complete. No callbacks run inside
    // storage publication, and no observer can see half an ownership transfer.
    static_assert(std::is_nothrow_copy_constructible_v<InventoryItem>);
    inventory.items.clear();
    for (const auto& entry : staged.items) {
        inventory.items.push_back(entry);
    }
    std::copy(staged.inventory_bitset.begin(), staged.inventory_bitset.end(), inventory.inventory_bitset.begin());
    area->items.swap(ground);
    for (size_t i = 0; i < transfers.size(); ++i) {
        auto& spatial = *spatial_rows[i];
        const bool drop = transfers[i].direction == ItemTransferDirection::drop;
        spatial.area = drop ? area_handle.id : object_invalid;
        if (drop) { spatial.position = actor_position; }
        spatial.velocity = {};
        spatial.angular_velocity = {};
    }
    return {ItemTransferStatus::success, static_cast<uint32_t>(transfers.size())};
}

const char* item_transfer_message(ItemTransferStatus status) noexcept
{
    switch (status) {
    case ItemTransferStatus::success:
        return "Item transferred";
    case ItemTransferStatus::invalid_input:
        return "Item request is invalid or stale";
    case ItemTransferStatus::invalid_ownership:
        return "Item is not available here; unequip carried equipment before dropping it";
    case ItemTransferStatus::invalid_inventory:
        return "Inventory or item layout is invalid";
    case ItemTransferStatus::out_of_reach:
        return "Move closer to the item";
    case ItemTransferStatus::blocked:
        return "Item transfer requires reachable walkable ground";
    case ItemTransferStatus::no_space:
        return "There is no room in the inventory";
    }
    return "Item transfer failed";
}

} // namespace nw
