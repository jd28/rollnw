#pragma once

#include <nw/objects/ObjectHandle.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nw {
struct Area;
struct AreaTile;
}

namespace nw::toolset {

inline constexpr int32_t k_area_door_any_hook_type = -1;

struct AreaDoorHook {
    glm::vec3 position{0.0f};
    glm::vec3 orientation{1.0f, 0.0f, 0.0f};
    ObjectHandle occupant{};
    int32_t type = -1;
    uint32_t tile_index = 0;
    uint32_t slot_index = 0;
};

// One active Area owns a cold snapshot. tile_offsets has tile_count + 1 rows;
// hooks for tile i occupy [tile_offsets[i], tile_offsets[i + 1]).
struct AreaDoorHookSnapshot {
    ObjectHandle area{};
    std::vector<uint32_t> tile_offsets;
    std::vector<AreaDoorHook> hooks;
    int32_t width = 0;
    int32_t height = 0;
};

// Replaces output from native Area tiles plus parsed SET door-slot rows. Invalid
// tiles and slots are dropped; invalid Area shape rejects the complete snapshot.
[[nodiscard]] bool build_area_door_hooks(
    const Area& area, AreaDoorHookSnapshot& output, std::string& diagnostic);

// Builds the same snapshot against a complete candidate tile array without
// mutating the Area. This is used to reject structural edits that would make
// their own inverse unsafe.
[[nodiscard]] bool build_area_door_hooks(
    const Area& area,
    std::span<const AreaTile> tiles,
    AreaDoorHookSnapshot& output,
    std::string& diagnostic);

// Searches the pointer tile and its eight neighbors. Nonnegative types must match
// exactly; k_area_door_any_hook_type accepts any hook. Occupied hooks are rejected
// unless occupied by exclude. Ties resolve by flat hook order so identical input
// always produces the same result.
[[nodiscard]] std::optional<AreaDoorHook> nearest_area_door_hook(
    const AreaDoorHookSnapshot& snapshot,
    glm::vec3 position,
    int32_t type,
    ObjectHandle exclude = ObjectHandle{});

// Projects the profile-owned Door state into the SET hook type. Generic Door
// models return k_area_door_any_hook_type; invalid profile data returns no type.
// C++ never reads DoorState fields.
[[nodiscard]] std::optional<int32_t> area_door_hook_type(ObjectHandle door);

} // namespace nw::toolset
