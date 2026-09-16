#include "area_tile_interaction.hpp"

#include <nw/formats/Tileset.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nw::toolset {
namespace {

constexpr float k_tile_size = 10.0f;

bool finite(glm::vec3 value) noexcept
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool valid_area_cells(const Area& area, uint32_t& count) noexcept
{
    if (area.width <= 0 || area.height <= 0 || !area.tileset
        || !std::isfinite(area.tileset->tile_height)
        || area.tileset->tile_height <= 0.0f) {
        return false;
    }
    const uint64_t count_64 = static_cast<uint64_t>(area.width)
        * static_cast<uint64_t>(area.height);
    if (count_64 > std::numeric_limits<uint32_t>::max()
        || count_64 != area.tiles.size()) {
        return false;
    }
    for (const auto& tile : area.tiles) {
        if (!std::isfinite(
                static_cast<float>(tile.height) * area.tileset->tile_height)) {
            return false;
        }
    }
    count = static_cast<uint32_t>(count_64);
    return true;
}

bool valid_cell(
    int32_t width, int32_t height, AreaTileCellCoord cell) noexcept
{
    return cell.x >= 0 && cell.x < width
        && cell.y >= 0 && cell.y < height;
}

} // namespace

void resolve_area_tile_pointer_actions(
    std::span<const AreaTilePointerInput> inputs,
    std::span<AreaTilePointerResult> outputs) noexcept
{
    std::fill(outputs.begin(), outputs.end(), AreaTilePointerResult{});
    if (inputs.size() != outputs.size()) {
        return;
    }

    for (size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        auto& output = outputs[index];
        const bool pointer_button
            = input.button == AreaTilePointerButton::primary
            || input.button == AreaTilePointerButton::secondary;
        if (input.modifier == AreaTilePointerModifier::blocked) {
            output.consumed = pointer_button;
            continue;
        }
        if (input.modifier == AreaTilePointerModifier::select) {
            if (input.button == AreaTilePointerButton::primary) {
                output = {
                    .action = AreaTilePointerAction::select,
                    .consumed = true,
                };
            } else if (input.button == AreaTilePointerButton::secondary) {
                output = {
                    .action = AreaTilePointerAction::cycle_variation,
                    .consumed = true,
                };
            }
            continue;
        }
        if (input.modifier != AreaTilePointerModifier::none) {
            output.consumed = pointer_button;
            continue;
        }
        if (input.button == AreaTilePointerButton::primary
            || (input.button == AreaTilePointerButton::secondary
                && input.secondary_paint_available)) {
            output = {
                .action = AreaTilePointerAction::paint,
                .consumed = true,
            };
        }
    }
}

AreaTilePointerResult resolve_area_tile_pointer_input(
    AreaTilePointerInput input) noexcept
{
    AreaTilePointerResult result;
    resolve_area_tile_pointer_actions(
        std::span<const AreaTilePointerInput>{&input, 1},
        std::span<AreaTilePointerResult>{&result, 1});
    return result;
}

AreaTilePointerAction resolve_area_tile_pointer_action(
    AreaTilePointerInput input) noexcept
{
    return resolve_area_tile_pointer_input(input).action;
}

void pick_area_tile_cells(
    const Area& area,
    std::span<const AreaTileCellRay> rays,
    std::span<AreaTileCellPick> output) noexcept
{
    std::fill(output.begin(), output.end(), AreaTileCellPick{});
    uint32_t tile_count = 0;
    if (rays.size() != output.size()
        || !valid_area_cells(area, tile_count)) {
        return;
    }

    constexpr float direction_epsilon = 1.0e-7f;
    constexpr float tie_epsilon = 1.0e-5f;
    for (size_t ray_index = 0; ray_index < rays.size(); ++ray_index) {
        const auto& ray = rays[ray_index];
        auto& result = output[ray_index];
        if (!finite(ray.origin) || !finite(ray.direction)) {
            continue;
        }
        const float direction_length = glm::length(ray.direction);
        if (!std::isfinite(direction_length)
            || direction_length <= direction_epsilon) {
            continue;
        }
        const glm::vec3 direction = ray.direction / direction_length;
        if (std::abs(direction.z) <= direction_epsilon) {
            result.status = AreaTileCellPickStatus::miss;
            continue;
        }

        float nearest_distance = std::numeric_limits<float>::max();
        uint32_t nearest_index = UINT32_MAX;
        glm::vec3 nearest_position{0.0f};
        for (uint32_t tile_index = 0; tile_index < tile_count; ++tile_index) {
            const float world_z = static_cast<float>(
                                      area.tiles[tile_index].height)
                * area.tileset->tile_height;
            const float distance = (world_z - ray.origin.z) / direction.z;
            if (!std::isfinite(distance) || distance < 0.0f) {
                continue;
            }
            const glm::vec3 position = ray.origin + direction * distance;
            const uint32_t x = tile_index
                % static_cast<uint32_t>(area.width);
            const uint32_t y = tile_index
                / static_cast<uint32_t>(area.width);
            const float min_x = static_cast<float>(x) * k_tile_size;
            const float min_y = static_cast<float>(y) * k_tile_size;
            if (position.x < min_x || position.x > min_x + k_tile_size
                || position.y < min_y || position.y > min_y + k_tile_size) {
                continue;
            }
            const bool nearer = distance + tie_epsilon < nearest_distance;
            const bool tied = std::abs(distance - nearest_distance)
                <= tie_epsilon;
            if (nearer || (tied && tile_index < nearest_index)) {
                nearest_distance = distance;
                nearest_index = tile_index;
                nearest_position = position;
            }
        }

        if (nearest_index == UINT32_MAX) {
            result.status = AreaTileCellPickStatus::miss;
            continue;
        }
        result = {
            .position = nearest_position,
            .distance = nearest_distance,
            .tile_index = nearest_index,
            .status = AreaTileCellPickStatus::hit,
        };
    }
}

AreaTileCellPick pick_area_tile_cell(
    const Area& area, const AreaTileCellRay& ray) noexcept
{
    AreaTileCellPick result;
    pick_area_tile_cells(
        area,
        std::span<const AreaTileCellRay>{&ray, 1},
        std::span<AreaTileCellPick>{&result, 1});
    return result;
}

void pick_area_tile_corners(
    const Area& area,
    std::span<const AreaTileCellPick> cell_hits,
    std::span<uint32_t> output) noexcept
{
    std::fill(output.begin(), output.end(), UINT32_MAX);
    uint32_t tile_count = 0;
    if (cell_hits.size() != output.size()
        || !valid_area_cells(area, tile_count)
        || area.width == std::numeric_limits<int32_t>::max()
        || area.height == std::numeric_limits<int32_t>::max()) {
        return;
    }

    for (size_t index = 0; index < cell_hits.size(); ++index) {
        const auto& hit = cell_hits[index];
        if (hit.status != AreaTileCellPickStatus::hit
            || hit.tile_index >= tile_count || !finite(hit.position)) {
            continue;
        }
        const uint32_t tile_x
            = hit.tile_index % static_cast<uint32_t>(area.width);
        const uint32_t tile_y
            = hit.tile_index / static_cast<uint32_t>(area.width);
        const float local_x
            = hit.position.x - static_cast<float>(tile_x) * k_tile_size;
        const float local_y
            = hit.position.y - static_cast<float>(tile_y) * k_tile_size;
        const uint32_t corner_x
            = tile_x + (local_x >= k_tile_size * 0.5f ? 1u : 0u);
        const uint32_t corner_y
            = tile_y + (local_y >= k_tile_size * 0.5f ? 1u : 0u);
        output[index] = corner_y * static_cast<uint32_t>(area.width + 1)
            + corner_x;
    }
}

uint32_t pick_area_tile_corner(
    const Area& area, const AreaTileCellPick& cell_hit) noexcept
{
    uint32_t result = UINT32_MAX;
    pick_area_tile_corners(area,
        std::span<const AreaTileCellPick>{&cell_hit, 1},
        std::span<uint32_t>{&result, 1});
    return result;
}

void resolve_area_tile_corner_cells(
    int32_t width,
    int32_t height,
    std::span<const uint32_t> corner_indices,
    std::span<AreaTileCornerCellSet> output) noexcept
{
    std::fill(output.begin(), output.end(), AreaTileCornerCellSet{});
    if (corner_indices.size() != output.size() || width <= 0 || height <= 0
        || width == std::numeric_limits<int32_t>::max()
        || height == std::numeric_limits<int32_t>::max()) {
        return;
    }
    const uint64_t corner_count = static_cast<uint64_t>(width + 1)
        * static_cast<uint64_t>(height + 1);
    if (corner_count > std::numeric_limits<uint32_t>::max()) {
        return;
    }

    const uint32_t stride = static_cast<uint32_t>(width + 1);
    for (size_t index = 0; index < corner_indices.size(); ++index) {
        const uint32_t corner_index = corner_indices[index];
        if (corner_index >= corner_count) {
            continue;
        }
        const int32_t corner_x
            = static_cast<int32_t>(corner_index % stride);
        const int32_t corner_y
            = static_cast<int32_t>(corner_index / stride);
        auto& result = output[index];
        for (int32_t y = corner_y - 1; y <= corner_y; ++y) {
            for (int32_t x = corner_x - 1; x <= corner_x; ++x) {
                if (valid_cell(width, height, {.x = x, .y = y})) {
                    result.tile_indices[result.count++] = static_cast<uint32_t>(
                        y * width + x);
                }
            }
        }
    }
}

AreaTileCornerCellSet resolve_area_tile_corner_cell(
    int32_t width, int32_t height, uint32_t corner_index) noexcept
{
    AreaTileCornerCellSet result;
    resolve_area_tile_corner_cells(width, height,
        std::span<const uint32_t>{&corner_index, 1},
        std::span<AreaTileCornerCellSet>{&result, 1});
    return result;
}

AreaTileLineResult append_area_tile_grid_line(
    int32_t width,
    int32_t height,
    AreaTileCellCoord from,
    AreaTileCellCoord to,
    std::span<uint8_t> visited,
    std::vector<uint32_t>& indices) noexcept
{
    const uint64_t count_64 = width > 0 && height > 0
        ? static_cast<uint64_t>(width) * static_cast<uint64_t>(height)
        : 0;
    if (count_64 == 0 || count_64 > std::numeric_limits<uint32_t>::max()
        || visited.size() != count_64
        || !valid_cell(width, height, from)
        || !valid_cell(width, height, to)) {
        return {.status = AreaTileLineStatus::invalid_input};
    }

    AreaTileLineResult result{
        .status = AreaTileLineStatus::success,
    };
    const auto append = [&](AreaTileCellCoord cell) {
        ++result.visited_count;
        const uint32_t index = static_cast<uint32_t>(
            static_cast<uint64_t>(cell.y) * static_cast<uint64_t>(width)
            + static_cast<uint64_t>(cell.x));
        if (visited[index] != 0u) {
            return true;
        }
        try {
            indices.push_back(index);
        } catch (const std::bad_alloc&) {
            result.status = AreaTileLineStatus::failed;
            return false;
        } catch (const std::length_error&) {
            result.status = AreaTileLineStatus::failed;
            return false;
        }
        visited[index] = 1u;
        ++result.appended_count;
        return true;
    };

    int32_t x = from.x;
    int32_t y = from.y;
    const int32_t dx = std::abs(to.x - from.x);
    const int32_t dy = std::abs(to.y - from.y);
    const int32_t step_x = from.x < to.x ? 1 : from.x > to.x ? -1
                                                             : 0;
    const int32_t step_y = from.y < to.y ? 1 : from.y > to.y ? -1
                                                             : 0;
    int32_t crossed_x = 0;
    int32_t crossed_y = 0;
    if (!append({.x = x, .y = y})) {
        return result;
    }
    while (crossed_x < dx || crossed_y < dy) {
        const int64_t x_boundary
            = (1 + 2 * static_cast<int64_t>(crossed_x)) * dy;
        const int64_t y_boundary
            = (1 + 2 * static_cast<int64_t>(crossed_y)) * dx;
        if (x_boundary == y_boundary) {
            x += step_x;
            y += step_y;
            ++crossed_x;
            ++crossed_y;
        } else if (x_boundary < y_boundary) {
            x += step_x;
            ++crossed_x;
        } else {
            y += step_y;
            ++crossed_y;
        }
        if (!append({.x = x, .y = y})) {
            return result;
        }
    }
    return result;
}

} // namespace nw::toolset
