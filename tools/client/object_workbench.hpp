#pragma once

#include <cstdint>
#include <nw/objects/ObjectHandle.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

// The current displayed workbench has one active surface. Unknown object types
// have no grid inventory/data-only exception; these are existing UI policies.
enum class ObjectWorkbenchSurface : uint8_t {
    details,
    locstring,
    sheet,
    variables,
    haks,
    classes,
    appearance,
    item_properties,
    feats,
    spells,
    inventory,
    spawns,
    sounds,
    store_inventory,
};

// In-process presentation protocol v1, compiled with all callers. Current facts
// are copied for one synchronous call; no resource/DOM lifetime is transferred.
// A false match means the active tab is unavailable or belongs to another object
// owner. Area-tab is used only for the existing selected-object header.
struct ObjectWorkbenchTarget {
    nw::ObjectHandle object{};
    ObjectWorkbenchSurface surface = ObjectWorkbenchSurface::details;
    bool matches_active_tab = false;
    bool area_tab = false;
    bool details_ready = false;
};

[[nodiscard]] ObjectWorkbenchSurface default_object_workbench_surface();
[[nodiscard]] std::optional<ObjectWorkbenchSurface> object_workbench_surface_from_name(std::string_view name) noexcept;
[[nodiscard]] bool data_workbench_only(ObjectType type, ObjectWorkbenchSurface surface) noexcept;
[[nodiscard]] bool object_has_grid_inventory(ObjectType type) noexcept;

struct PlacedItemOwnerRow {
    ObjectHandle object{};
    uint32_t parent = UINT32_MAX;
    uint32_t root = 0;
    // Only placed roots and directly equipped items affect a world visual.
    ObjectHandle visual_object{};
};

struct PlacedItemOwners {
    std::vector<PlacedItemOwnerRow> rows;
    std::string error;
};

// Cold, kernel-thread snapshot of placed inventory owners and all their items.
// Parent/root indices address rows; roots have no parent. Borrows no objects after
// return. Invalid, unresolved or repeated ownership rejects the complete batch.
[[nodiscard]] PlacedItemOwners collect_placed_item_owners(ObjectHandle area);

} // namespace nw::toolset
