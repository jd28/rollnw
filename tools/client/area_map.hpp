#pragma once

#include <nw/objects/Area.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nw::toolset {

inline constexpr uint32_t kAreaMapPreferredTilePixels = 32;
inline constexpr uint32_t kAreaMapMaxDimension = 2048;

struct AreaMapSource {
    std::string resref;
    std::string tileset;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<nw::AreaTile> tiles;
};

struct AreaMapWriteResult {
    size_t written = 0;
    size_t degraded = 0;
    size_t failed = 0;
    std::string first_warning;
    std::string first_error;
};

struct AreaMapOutputSize {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tile_pixels = 0;
};

[[nodiscard]] std::optional<AreaMapOutputSize> area_map_output_size(
    int32_t area_width, int32_t area_height) noexcept;

/// Copies the map inputs from live areas so the larger object graphs can be
/// released before the maps are composed.
[[nodiscard]] std::vector<AreaMapSource> collect_area_map_sources(
    std::span<const nw::Area* const> areas);

/// Returns the derived map path for an area resource reference.
[[nodiscard]] std::filesystem::path project_area_map_path(
    const std::filesystem::path& project_dir, std::string_view area_resref);

/// Composes and writes a batch of whole-area maps. Invalid sources are dropped
/// individually and reported in the result; valid sources continue.
[[nodiscard]] AreaMapWriteResult write_project_area_maps(
    const std::filesystem::path& project_dir,
    std::span<const AreaMapSource> sources);

} // namespace nw::toolset
