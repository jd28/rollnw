#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace nw {
struct Area;
} // namespace nw

namespace nw::render {

struct AreaTileGridVertex {
    glm::vec3 position{0.0f};
    glm::vec3 opposite{0.0f};
    glm::vec2 extrusion{0.0f};
    glm::vec4 color{0.0f};
};

struct AreaTileGridDebugGeometry {
    std::vector<AreaTileGridVertex> vertices;
    std::vector<uint32_t> indices;
};

// Batch transform contract: input is one dense width*height Area tile array
// with a finite positive tileset height. Output is one caller-owned screen-line
// triangle batch. Contiguous equal-height edges are merged; height breaks occur
// at both elevations. Cost is O(width*height) CPU reads and worst-case output.
// Validation and horizontal edges read linearly; vertical edges stride by width,
// and height-change branches follow the authored discontinuities. The Area is
// the single owning container; all of its tiles are processed as one batch.
// Invalid input or allocation failure clears output.
[[nodiscard]] bool build_area_tile_grid_debug_geometry(
    const nw::Area& area,
    AreaTileGridDebugGeometry& output) noexcept;

} // namespace nw::render
