#pragma once

#include <cstdint>
#include <nw/objects/ObjectHandle.hpp>

namespace nw::toolset {

// The current displayed workbench has one active surface. Unknown object types
// have no grid inventory/data-only exception; these are existing UI policies.
enum class ObjectWorkbenchSurface : uint8_t {
    details,
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

[[nodiscard]] ObjectWorkbenchSurface default_object_workbench_surface();
[[nodiscard]] bool data_workbench_only(ObjectType type, ObjectWorkbenchSurface surface) noexcept;
[[nodiscard]] bool object_has_grid_inventory(ObjectType type) noexcept;

} // namespace nw::toolset
