#pragma once

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
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

struct NewAreaWriteResult {
    std::filesystem::path relative_path;
    bool saved = false;
    bool published = false;
    std::string error;
};

// Returns the first ungrouped, flat, uncrossed tile whose four corners use the
// SET's default terrain. SET row order is the only tie-breaker.
[[nodiscard]] bool canonical_area_ground_tile(
    const Tileset& tileset, AreaTile& output) noexcept;

// The batch is rejected before serialization when any request is invalid.
// Requests create CAF resources only; dimensions outside 2..32, duplicate
// resources, unavailable SETs, and destinations outside the active native
// module root reject the complete batch without writing files.
[[nodiscard]] PreparedNewAreas prepare_new_areas(
    const std::filesystem::path& project,
    std::span<const NewAreaRequest> requests);

// Revalidates the prepared batch, creates every CAF exclusively, then refreshes
// the resource registry once. Any write or refresh failure removes files created
// by this call, so callers see either the complete batch or none of it.
[[nodiscard]] std::vector<NewAreaWriteResult> publish_new_areas(
    const PreparedNewAreas& prepared);

} // namespace nw::toolset
