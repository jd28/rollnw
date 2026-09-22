#include "area_tile_grid.hpp"

#include "../formats/Tileset.hpp"
#include "../objects/Area.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>

namespace nw::render {

namespace {

void append_area_tile_grid_segment(
    AreaTileGridDebugGeometry& output,
    const glm::vec3& a,
    const glm::vec3& b)
{
    constexpr glm::vec4 k_color{0.42f, 0.52f, 0.62f, 0.22f};
    const glm::vec3 delta = b - a;
    if (glm::dot(delta, delta) <= 1.0e-10f) {
        return;
    }

    const auto base = static_cast<uint32_t>(output.vertices.size());
    output.vertices.push_back({a, b, {1.0f, 0.0f}, k_color});
    output.vertices.push_back({b, a, {-1.0f, 0.0f}, k_color});
    output.vertices.push_back({b, a, {1.0f, 0.0f}, k_color});
    output.vertices.push_back({a, b, {-1.0f, 0.0f}, k_color});
    output.indices.insert(output.indices.end(), {
                                                    base,
                                                    base + 1,
                                                    base + 2,
                                                    base,
                                                    base + 2,
                                                    base + 3,
                                                });
}

} // namespace

bool build_area_tile_grid_debug_geometry(
    const nw::Area& area,
    AreaTileGridDebugGeometry& output) noexcept
{
    output.vertices.clear();
    output.indices.clear();
    const uint64_t tile_count = area.width > 0 && area.height > 0
        ? static_cast<uint64_t>(area.width)
            * static_cast<uint64_t>(area.height)
        : 0;
    if (!area.tileset || tile_count == 0
        || tile_count != area.tiles.size()
        || !std::isfinite(area.tileset->tile_height)
        || area.tileset->tile_height <= 0.0f
        || tile_count > std::numeric_limits<uint32_t>::max() / 16u) {
        return false;
    }

    constexpr float k_tile_size = 10.0f;
    constexpr float k_offset = 0.14f;
    const auto tile_z = [&area](int32_t x, int32_t y) {
        const auto& tile = area.tiles[static_cast<size_t>(y)
                * static_cast<size_t>(area.width)
            + static_cast<size_t>(x)];
        return static_cast<float>(tile.height)
            * area.tileset->tile_height
            + k_offset;
    };
    for (const auto& tile : area.tiles) {
        const float z = static_cast<float>(tile.height)
                * area.tileset->tile_height
            + k_offset;
        if (!std::isfinite(z)) {
            return false;
        }
    }
    try {
        const size_t flat_segment_count
            = static_cast<size_t>(area.width)
            + static_cast<size_t>(area.height) + 2u;
        output.vertices.reserve(flat_segment_count * 4u);
        output.indices.reserve(flat_segment_count * 6u);
        const auto append_horizontal_runs = [&](int32_t y, const auto& sample) {
            std::optional<float> run_z;
            int32_t run_start = 0;
            for (int32_t x = 0; x <= area.width; ++x) {
                const std::optional<float> z
                    = x < area.width ? sample(x) : std::nullopt;
                if (run_z && (!z || *z != *run_z)) {
                    append_area_tile_grid_segment(output,
                        {static_cast<float>(run_start) * k_tile_size,
                            static_cast<float>(y) * k_tile_size, *run_z},
                        {static_cast<float>(x) * k_tile_size,
                            static_cast<float>(y) * k_tile_size, *run_z});
                    run_z.reset();
                }
                if (z && !run_z) {
                    run_start = x;
                    run_z = z;
                }
            }
        };
        const auto append_vertical_runs = [&](int32_t x, const auto& sample) {
            std::optional<float> run_z;
            int32_t run_start = 0;
            for (int32_t y = 0; y <= area.height; ++y) {
                const std::optional<float> z
                    = y < area.height ? sample(y) : std::nullopt;
                if (run_z && (!z || *z != *run_z)) {
                    append_area_tile_grid_segment(output,
                        {static_cast<float>(x) * k_tile_size,
                            static_cast<float>(run_start) * k_tile_size, *run_z},
                        {static_cast<float>(x) * k_tile_size,
                            static_cast<float>(y) * k_tile_size, *run_z});
                    run_z.reset();
                }
                if (z && !run_z) {
                    run_start = y;
                    run_z = z;
                }
            }
        };
        for (int32_t y = 0; y <= area.height; ++y) {
            append_horizontal_runs(y, [&](int32_t x) -> std::optional<float> {
                const bool has_above = y < area.height;
                const float above_z = has_above ? tile_z(x, y) : 0.0f;
                return has_above ? std::optional{above_z} : std::nullopt;
            });
            append_horizontal_runs(y, [&](int32_t x) -> std::optional<float> {
                const bool has_above = y < area.height;
                const bool has_below = y > 0;
                const float above_z = has_above ? tile_z(x, y) : 0.0f;
                const float below_z = has_below ? tile_z(x, y - 1) : 0.0f;
                return has_below && (!has_above || below_z != above_z)
                    ? std::optional{below_z}
                    : std::nullopt;
            });
        }
        for (int32_t x = 0; x <= area.width; ++x) {
            append_vertical_runs(x, [&](int32_t y) -> std::optional<float> {
                const bool has_right = x < area.width;
                const float right_z = has_right ? tile_z(x, y) : 0.0f;
                return has_right ? std::optional{right_z} : std::nullopt;
            });
            append_vertical_runs(x, [&](int32_t y) -> std::optional<float> {
                const bool has_right = x < area.width;
                const bool has_left = x > 0;
                const float right_z = has_right ? tile_z(x, y) : 0.0f;
                const float left_z = has_left ? tile_z(x - 1, y) : 0.0f;
                return has_left && (!has_right || left_z != right_z)
                    ? std::optional{left_z}
                    : std::nullopt;
            });
        }
    } catch (const std::bad_alloc&) {
        output.vertices.clear();
        output.indices.clear();
        return false;
    } catch (const std::length_error&) {
        output.vertices.clear();
        output.indices.clear();
        return false;
    }
    return true;
}

} // namespace nw::render
