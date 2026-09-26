#pragma once

#include "area_map.hpp"

#include <nw/objects/Area.hpp>
#include <nw/resources/assets.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace nw {
struct Tileset;
}

namespace nw::toolset {

inline constexpr int32_t kMinimumNewAreaDimension = 2;
inline constexpr int32_t kMaximumNewAreaDimension = 32;

struct NewAreaRequest {
    std::filesystem::path directory;
    std::string resref;
    std::string name;
    Resref tileset;
    int32_t width = 4;
    int32_t height = 4;
};

struct PreparedNewArea {
    NewAreaRequest request;
    Resource resource;
    std::filesystem::path target;
    std::filesystem::path relative_path;
    std::string bytes;
};

struct PreparedNewAreas {
    std::filesystem::path project;
    uint64_t resource_generation = 0;
    std::vector<PreparedNewArea> rows;
    std::vector<AreaMapSource> map_sources;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

struct NewAreaWriteResult {
    std::filesystem::path relative_path;
    bool saved = false;
    bool published = false;
    std::string error;
};

struct NewAreaPublishResult {
    std::vector<NewAreaWriteResult> rows;
    AreaMapWriteResult maps;
};

// Returns the first ungrouped, flat, uncrossed tile whose four corners use the
// SET's default terrain. SET row order is the only tie-breaker.
[[nodiscard]] bool canonical_area_ground_tile(
    const Tileset& tileset, AreaTile& output) noexcept;

// Returns the first compatible variation selected by
// collect_area_ground_tiles. This singular query is for tileset availability;
// area generation uses the plural catalog and batch transform below.
[[nodiscard]] bool preferred_area_ground_tile(
    const Tileset& tileset, AreaTile& output);

// Collects ungrouped, flat, uncrossed default-terrain SET rows that can share
// one initial area height. The first row is the first authored shadow caster
// when available; otherwise it is the first canonical SET row. Remaining rows
// retain SET order. Output is empty on ordinary invalid input.
[[nodiscard]] bool collect_area_ground_tiles(
    const Tileset& tileset, std::vector<AreaTile>& output);

// Fills a borrowed dense tile batch from the candidate rows using a stable
// seed. Candidate zero occurs at least once, multiple candidate IDs produce at
// least two IDs when the output has room, and orientations vary when only one
// ID exists. Invalid candidates reject without modifying output.
[[nodiscard]] bool generate_area_ground_tiles(
    std::span<AreaTile> output,
    std::span<const AreaTile> candidates,
    uint64_t seed) noexcept;

// The batch is rejected before serialization when any request is invalid.
// Dimensions outside 2..32, duplicate resources, unavailable SETs, and
// destinations outside the active native module root reject the complete batch
// without writing files. The prepared map-source batch has the same order and
// count as rows.
[[nodiscard]] PreparedNewAreas prepare_new_areas(
    const std::filesystem::path& project,
    std::span<const NewAreaRequest> requests);

// Revalidates the prepared batch, creates every CAF exclusively, refreshes the
// resource registry once, then writes the derived map batch. Any CAF write or
// refresh failure removes files created by this call, so callers see either the
// complete authored batch or none of it. A map failure is reported separately
// and preserves the successfully published CAF resources.
[[nodiscard]] NewAreaPublishResult publish_new_areas(
    const PreparedNewAreas& prepared);

} // namespace nw::toolset
