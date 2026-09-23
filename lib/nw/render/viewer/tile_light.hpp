#pragma once

#include "preview_scene.hpp"

#include <nw/model/Mdl.hpp>
#include <nw/util/string.hpp>

#include <glm/glm.hpp>

#include <cstdint>

namespace nw::render::viewer {

/// Returns true when any tile light slot is non-zero (i.e. the tile has authored lights).
bool has_tile_light_slots(const SceneTileLightSlots& slots) noexcept;

/// Parsed suffix of a tile light dummy/light node: "<tile>{ml,sl}{1,2}".
struct TileLightSlot {
    bool valid = false;
    bool is_main = true;
    bool second = false; // slot index 2 when true, else slot 1
};

/// Parses a node name to determine which tile light slot it drives.
TileLightSlot parse_tile_light_slot(StringView name) noexcept;

/// Returns the tile's colour-index value for the given slot.
uint8_t tile_slot_color_index(const SceneTileLightSlots& slots, const TileLightSlot& slot) noexcept;

/// Looks up the RGB colour for a tile light index from tilecolor.2da.
glm::vec3 tile_color_from_index(uint8_t index);

/// Resolves NWN's TILE_MAIN_LIGHT_COLOR_* range [0, 31]. Invalid values are
/// black so malformed data cannot select an unrelated palette entry.
glm::vec3 tile_main_light_color(uint8_t value) noexcept;

/// Resolves NWN's TILE_SOURCE_LIGHT_COLOR_* range [0, 15]. Invalid values are
/// black so malformed data cannot select an unrelated palette entry.
glm::vec3 tile_source_light_color(uint8_t value) noexcept;

/// Resolves the final colour for a model light node driven by a tile's light slots.
glm::vec3 tile_slot_color_for_model_light(
    const nw::model::LightNode& light,
    const SceneTileLightSlots& slots) noexcept;

/// Decides whether a model light node behaves as a main (ambient) or source
/// (directional) tile light. A valid node-name suffix is authoritative;
/// unrecognized nodes fall back to their authored radius.
bool model_light_is_main_tile_slot(const nw::model::LightNode& light) noexcept;

} // namespace nw::render::viewer
