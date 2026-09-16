#pragma once

#include "../ui/rml_generated_texture.hpp"
#include "area_tile_brush.hpp"

#include <nw/objects/ObjectHandle.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

enum class AreaTilePaletteStatus : uint8_t {
    empty,
    ready,
    invalid_input,
    failed,
};

enum class AreaTilePaletteRowKind : uint8_t {
    folder,
    action,
};

struct AreaTilePaletteRow {
    AreaTilePaletteRowKind kind = AreaTilePaletteRowKind::folder;
    uint32_t parent = UINT32_MAX;
    uint32_t child_offset = 0;
    uint32_t child_count = 0;
    uint32_t subtree_end = 0;
    AreaTileBrush brush;
    std::string label;
    std::string category;
    std::string resref;
    std::string image_map_2d;
    std::string thumbnail_source;
};

struct AreaTilePalette {
    ObjectHandle area{};
    std::string tileset;
    uint64_t resource_generation = 0;
    std::vector<AreaTilePaletteRow> rows;
    std::vector<uint32_t> child_rows;
    std::vector<uint32_t> matches;
    std::vector<RmlGeneratedTexture> textures;
    uint32_t root_folder = UINT32_MAX;
    uint32_t current_folder = UINT32_MAX;
    AreaTilePaletteStatus status = AreaTilePaletteStatus::empty;
    std::string diagnostic;
};

[[nodiscard]] bool build_area_tile_palette(
    ObjectHandle area, AreaTilePalette& output);

[[nodiscard]] bool filter_area_tile_palette(
    AreaTilePalette& palette, std::string_view query);

[[nodiscard]] bool enter_area_tile_palette_folder(
    AreaTilePalette& palette, uint32_t row_index);

[[nodiscard]] bool leave_area_tile_palette_folder(
    AreaTilePalette& palette);

// Decodes only the visible match range. Missing thumbnail resources leave the
// corresponding row text-only; malformed ranges are rejected without writes.
[[nodiscard]] bool load_area_tile_palette_thumbnails(
    AreaTilePalette& palette, int match_begin, int match_end);

} // namespace nw::toolset
