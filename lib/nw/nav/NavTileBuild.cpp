#include "NavTileBuild.hpp"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace nw::nav {
namespace {

class SilentRecastContext : public rcContext {
public:
    SilentRecastContext() noexcept
        : rcContext(false)
    {
    }
};

template <typename T, void (*Free)(T*)>
class RecastHandle {
public:
    explicit RecastHandle(T* value = nullptr) noexcept
        : value_{value}
    {
    }
    ~RecastHandle() { Free(value_); }
    RecastHandle(const RecastHandle&) = delete;
    RecastHandle& operator=(const RecastHandle&) = delete;

    [[nodiscard]] T* get() const noexcept { return value_; }
    [[nodiscard]] T& operator*() const noexcept { return *value_; }

private:
    T* value_ = nullptr;
};

struct NavTileScratch {
    bool active = false;
    Vector<float> surface_vertices;
    Vector<int> surface_indices;
    Vector<unsigned char> surface_areas;
    Vector<float> obstacle_vertices;
    Vector<int> obstacle_indices;
    Vector<unsigned char> obstacle_areas;
    Vector<float> link_vertices;
    Vector<float> link_radii;
    Vector<unsigned char> link_directions;
    Vector<unsigned char> link_areas;
    Vector<unsigned short> link_flags;
    Vector<unsigned int> link_user_ids;
};

thread_local NavTileScratch g_scratch;

class ScratchGuard {
public:
    explicit ScratchGuard(NavTileScratch& scratch) noexcept
        : scratch_{scratch}
        , acquired_{!scratch.active}
    {
        if (acquired_) scratch_.active = true;
    }
    ~ScratchGuard()
    {
        if (acquired_) scratch_.active = false;
    }
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }

private:
    NavTileScratch& scratch_;
    bool acquired_ = false;
};

bool finite(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool valid_config(const NavTileBuildConfig& config) noexcept
{
    if (!(config.cell_size > 0.0f) || !(config.cell_height > 0.0f)
        || !std::isfinite(config.cell_size)
        || !std::isfinite(config.cell_height)
        || !(config.agent_height > 0.0f) || config.agent_max_climb < 0.0f
        || !std::isfinite(config.agent_height)
        || !std::isfinite(config.agent_max_climb)
        || !std::isfinite(config.max_simplification_error)
        || config.max_simplification_error < 0.0f
        || !std::isfinite(config.max_edge_length)
        || config.max_edge_length < 0.0f
        || !std::isfinite(config.detail_sample_distance)
        || config.detail_sample_distance < 0.9f
        || !std::isfinite(config.detail_sample_max_error)
        || config.detail_sample_max_error < 0.0f
        || config.verts_per_polygon < 3
        || config.verts_per_polygon > DT_VERTS_PER_POLYGON) {
        return false;
    }
    const float horizontal_cells = nav_tile_size / config.cell_size;
    const float vertical_cells = nav_tile_size / config.cell_height;
    const float height_cells = std::ceil(
        config.agent_height / config.cell_height);
    const float climb_cells = std::floor(
        config.agent_max_climb / config.cell_height);
    const double border_cells
        = static_cast<double>(config.erosion_cells) + 3.0;
    const double grid_side
        = static_cast<double>(horizontal_cells) + border_cells * 2.0;
    const double grid_cells = grid_side * grid_side;
    const double maximum_edge_cells
        = static_cast<double>(config.max_edge_length)
        / static_cast<double>(config.cell_size);
    return std::isfinite(horizontal_cells) && std::isfinite(vertical_cells)
        && std::isfinite(grid_cells)
        && grid_side <= static_cast<double>(std::numeric_limits<int>::max())
        // Recast uses signed-int width * height products internally.
        && grid_cells <= static_cast<double>(std::numeric_limits<int>::max())
        && height_cells <= static_cast<float>(std::numeric_limits<int>::max())
        && climb_cells <= static_cast<float>(std::numeric_limits<int>::max())
        && maximum_edge_cells
        <= static_cast<double>(std::numeric_limits<int>::max())
        && std::abs(horizontal_cells - std::round(horizontal_cells)) <= 1.0e-4f
        && std::abs(vertical_cells - std::round(vertical_cells)) <= 1.0e-4f;
}

bool valid_flat_geometry(std::span<const glm::vec3> vertices,
    std::span<const uint32_t> indices, size_t triangle_rows) noexcept
{
    if (indices.size() % 3 != 0 || indices.size() / 3 != triangle_rows
        || vertices.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    for (const auto& vertex : vertices) {
        if (!finite(vertex)) return false;
    }
    for (uint32_t index : indices) {
        if (index >= vertices.size()
            || index > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            return false;
        }
    }
    return true;
}

void append_recast_vertex(Vector<float>& output, const glm::vec3& vertex)
{
    output.push_back(vertex.x);
    output.push_back(vertex.z);
    output.push_back(vertex.y);
}

bool triangle_overlaps_bounds(const glm::vec3& a, const glm::vec3& b,
    const glm::vec3& c, const std::array<float, 3>& minimum,
    const std::array<float, 3>& maximum) noexcept
{
    const float min_x = std::min({a.x, b.x, c.x});
    const float max_x = std::max({a.x, b.x, c.x});
    const float min_y = std::min({a.y, b.y, c.y});
    const float max_y = std::max({a.y, b.y, c.y});
    return max_x >= minimum[0] && min_x <= maximum[0]
        && max_y >= minimum[2] && min_y <= maximum[2];
}

bool append_triangle(std::span<const glm::vec3> vertices,
    std::span<const uint32_t> indices, uint32_t triangle,
    const std::array<float, 3>& minimum, const std::array<float, 3>& maximum,
    unsigned char area, Vector<float>& output_vertices,
    Vector<int>& output_indices, Vector<unsigned char>& output_areas)
{
    if (triangle >= indices.size() / 3) return false;
    const size_t offset = static_cast<size_t>(triangle) * 3;
    const uint32_t ia = indices[offset];
    const uint32_t ib = indices[offset + 1];
    const uint32_t ic = indices[offset + 2];
    if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) return false;
    const auto& a = vertices[ia];
    const auto& b = vertices[ib];
    const auto& c = vertices[ic];
    if (!finite(a) || !finite(b) || !finite(c)) return false;
    if (!triangle_overlaps_bounds(a, b, c, minimum, maximum)) return true;
    if (output_vertices.size() / 3
        > static_cast<size_t>(std::numeric_limits<int>::max() - 3)) {
        return false;
    }
    const int base = static_cast<int>(output_vertices.size() / 3);
    append_recast_vertex(output_vertices, a);
    append_recast_vertex(output_vertices, b);
    append_recast_vertex(output_vertices, c);
    output_indices.insert(output_indices.end(), {base, base + 1, base + 2});
    output_areas.push_back(area);
    return true;
}

bool authored_height_at(std::span<const float> vertices, float x, float z,
    float reference_height, float cell_height, float& height) noexcept
{
    constexpr float k_barycentric_epsilon = 1.0e-4f;
    constexpr float k_height_epsilon = 1.0e-4f;
    bool found = false;
    bool ambiguous = false;
    float best_height = 0.0f;
    float best_distance = std::numeric_limits<float>::max();
    for (size_t triangle = 0; triangle < vertices.size() / 9; ++triangle) {
        const float* a = vertices.data() + triangle * 9;
        const float* b = a + 3;
        const float* c = b + 3;
        const float denominator = (b[2] - c[2]) * (a[0] - c[0])
            + (c[0] - b[0]) * (a[2] - c[2]);
        if (std::abs(denominator) <= std::numeric_limits<float>::epsilon()) {
            continue;
        }
        const float first
            = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2]))
            / denominator;
        const float second
            = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2]))
            / denominator;
        const float third = 1.0f - first - second;
        if (first < -k_barycentric_epsilon
            || second < -k_barycentric_epsilon
            || third < -k_barycentric_epsilon) {
            continue;
        }
        const float candidate
            = first * a[1] + second * b[1] + third * c[1];
        if (!std::isfinite(candidate)) return false;
        const float distance = std::abs(candidate - reference_height);
        if (!found || distance + k_height_epsilon < best_distance) {
            found = true;
            ambiguous = false;
            best_height = candidate;
            best_distance = distance;
        } else if (std::abs(distance - best_distance) <= k_height_epsilon
            && std::abs(candidate - best_height)
                > cell_height + k_height_epsilon) {
            ambiguous = true;
        }
    }
    if (!found || ambiguous) return false;
    height = best_height;
    return true;
}

bool normalize_authored_heights(rcPolyMesh& poly_mesh,
    rcPolyMeshDetail& detail_mesh, std::span<const float> surface_vertices,
    float cell_height, size_t& normalized_vertex_count) noexcept
{
    constexpr float k_maximum_quantized_height = 0xffff;
    normalized_vertex_count = 0;
    for (int vertex = 0; vertex < poly_mesh.nverts; ++vertex) {
        unsigned short* value = poly_mesh.verts + vertex * 3;
        const float x = poly_mesh.bmin[0] + value[0] * poly_mesh.cs;
        const float generated_height
            = poly_mesh.bmin[1] + value[1] * poly_mesh.ch;
        const float z = poly_mesh.bmin[2] + value[2] * poly_mesh.cs;
        float authored_height = 0.0f;
        if (!authored_height_at(surface_vertices, x, z, generated_height,
                cell_height, authored_height)) {
            return false;
        }
        const float quantized = std::round(
            (authored_height - poly_mesh.bmin[1]) / poly_mesh.ch);
        if (!std::isfinite(quantized) || quantized < 0.0f
            || quantized > k_maximum_quantized_height) {
            return false;
        }
        value[1] = static_cast<unsigned short>(quantized);
        ++normalized_vertex_count;
    }
    for (int vertex = 0; vertex < detail_mesh.nverts; ++vertex) {
        float* value = detail_mesh.verts + vertex * 3;
        float authored_height = 0.0f;
        if (!authored_height_at(surface_vertices, value[0], value[2],
                value[1], cell_height, authored_height)) {
            return false;
        }
        value[1] = authored_height;
        ++normalized_vertex_count;
    }
    return true;
}

uint32_t door_user_id(uint32_t door_index, uint8_t side, bool& valid) noexcept
{
    if (side > 1 || door_index >= (UINT32_MAX - 1u) / 2u) {
        valid = false;
        return 0;
    }
    return 1u + door_index * 2u + side;
}

bool triangle_tile_bounds(std::span<const glm::vec3> vertices,
    std::span<const uint32_t> indices, size_t triangle, uint32_t width,
    uint32_t height, float border, NavTileCoord& minimum,
    NavTileCoord& maximum) noexcept
{
    const size_t offset = triangle * 3;
    const auto& a = vertices[indices[offset]];
    const auto& b = vertices[indices[offset + 1]];
    const auto& c = vertices[indices[offset + 2]];
    const double min_x = std::min({static_cast<double>(a.x),
        static_cast<double>(b.x), static_cast<double>(c.x)});
    const double max_x = std::max({static_cast<double>(a.x),
        static_cast<double>(b.x), static_cast<double>(c.x)});
    const double min_y = std::min({static_cast<double>(a.y),
        static_cast<double>(b.y), static_cast<double>(c.y)});
    const double max_y = std::max({static_cast<double>(a.y),
        static_cast<double>(b.y), static_cast<double>(c.y)});
    const double tile_size = static_cast<double>(nav_tile_size);
    const double expanded = static_cast<double>(border);
    const double min_tile_x = std::ceil((min_x - expanded) / tile_size) - 1.0;
    const double max_tile_x = std::floor((max_x + expanded) / tile_size);
    const double min_tile_y = std::ceil((min_y - expanded) / tile_size) - 1.0;
    const double max_tile_y = std::floor((max_y + expanded) / tile_size);
    if (max_tile_x < 0.0 || max_tile_y < 0.0
        || min_tile_x >= static_cast<double>(width)
        || min_tile_y >= static_cast<double>(height)) {
        return false;
    }
    minimum.x = static_cast<uint32_t>(std::max(0.0, min_tile_x));
    minimum.y = static_cast<uint32_t>(std::max(0.0, min_tile_y));
    maximum.x = static_cast<uint32_t>(std::min(
        static_cast<double>(width - 1), max_tile_x));
    maximum.y = static_cast<uint32_t>(std::min(
        static_cast<double>(height - 1), max_tile_y));
    return minimum.x <= maximum.x && minimum.y <= maximum.y;
}

} // namespace

void NavTileTriangleRanges::clear()
{
    offsets.clear();
    triangles.clear();
}

std::span<const uint32_t> NavTileTriangleRanges::tile(
    NavTileCoord coordinate, uint32_t width) const noexcept
{
    if (width == 0 || coordinate.x >= width) return {};
    const size_t tile_index
        = static_cast<size_t>(coordinate.y) * width + coordinate.x;
    if (tile_index + 1 >= offsets.size()) return {};
    const uint32_t begin = offsets[tile_index];
    const uint32_t end = offsets[tile_index + 1];
    if (begin > end || end > triangles.size()) return {};
    return std::span<const uint32_t>{triangles}.subspan(begin, end - begin);
}

NavTileRangeStats build_nav_tile_triangle_ranges(
    std::span<const glm::vec3> vertices,
    std::span<const uint32_t> indices,
    uint32_t width,
    uint32_t height,
    float border,
    NavTileTriangleRanges& output)
{
    NavTileRangeStats stats;
    stats.input_triangle_count = indices.size() / 3;
    output.clear();
    const uint64_t tile_count = static_cast<uint64_t>(width) * height;
    if (width == 0 || height == 0 || !std::isfinite(border) || border < 0.0f
        || tile_count > std::numeric_limits<uint32_t>::max()
        || stats.input_triangle_count > std::numeric_limits<uint32_t>::max()
        || !valid_flat_geometry(vertices, indices, stats.input_triangle_count)) {
        return stats;
    }
    stats.tile_count = static_cast<size_t>(tile_count);
    output.offsets.assign(stats.tile_count + 1, 0u);

    for (size_t triangle = 0; triangle < stats.input_triangle_count; ++triangle) {
        NavTileCoord minimum;
        NavTileCoord maximum;
        if (!triangle_tile_bounds(vertices, indices, triangle, width, height,
                border, minimum, maximum)) {
            continue;
        }
        for (uint32_t y = minimum.y; y <= maximum.y; ++y) {
            for (uint32_t x = minimum.x; x <= maximum.x; ++x) {
                const size_t tile_index
                    = static_cast<size_t>(y) * width + x;
                if (output.offsets[tile_index + 1]
                    == std::numeric_limits<uint32_t>::max()) {
                    output.clear();
                    return stats;
                }
                ++output.offsets[tile_index + 1];
            }
        }
    }
    for (size_t tile = 0; tile < stats.tile_count; ++tile) {
        const uint64_t next = static_cast<uint64_t>(output.offsets[tile])
            + output.offsets[tile + 1];
        if (next > std::numeric_limits<uint32_t>::max()) {
            output.clear();
            return stats;
        }
        output.offsets[tile + 1] = static_cast<uint32_t>(next);
    }
    output.triangles.resize(output.offsets.back());
    Vector<uint32_t> cursors = output.offsets;
    for (size_t triangle = 0; triangle < stats.input_triangle_count; ++triangle) {
        NavTileCoord minimum;
        NavTileCoord maximum;
        if (!triangle_tile_bounds(vertices, indices, triangle, width, height,
                border, minimum, maximum)) {
            continue;
        }
        for (uint32_t y = minimum.y; y <= maximum.y; ++y) {
            for (uint32_t x = minimum.x; x <= maximum.x; ++x) {
                const size_t tile_index
                    = static_cast<size_t>(y) * width + x;
                output.triangles[cursors[tile_index]++]
                    = static_cast<uint32_t>(triangle);
            }
        }
    }
    stats.overlap_count = output.triangles.size();
    stats.status = NavStatus::ok;
    return stats;
}

NavTileData::~NavTileData() { clear(); }

NavTileData::NavTileData(NavTileData&& other) noexcept
    : data_{other.data_}
    , size_{other.size_}
{
    other.data_ = nullptr;
    other.size_ = 0;
}

NavTileData& NavTileData::operator=(NavTileData&& other) noexcept
{
    if (this != &other) {
        clear();
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

unsigned char* NavTileData::release() noexcept
{
    unsigned char* result = data_;
    data_ = nullptr;
    size_ = 0;
    return result;
}

void NavTileData::clear() noexcept
{
    dtFree(data_);
    data_ = nullptr;
    size_ = 0;
}

NavTileBuildStats build_nav_tile_data(const NavTileBuildInput& input,
    const NavTileBuildConfig& config, NavTileData& output)
{
    const auto started = std::chrono::steady_clock::now();
    NavTileBuildStats stats;
    stats.input_surface_triangle_count = input.surface_triangles.size();
    stats.input_obstacle_triangle_count = input.obstacle_triangles.size();
    stats.input_door_link_count = input.door_link_indices.size();
    output.clear();

    ScratchGuard guard{g_scratch};
    if (!guard.acquired() || !valid_config(config)
        || input.tile.x > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || input.tile.y > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || input.surface_triangles.size()
            > static_cast<size_t>(std::numeric_limits<int>::max() / 3)
        || input.obstacle_triangles.size()
            > static_cast<size_t>(std::numeric_limits<int>::max() / 3)
        || input.door_link_indices.size()
            > static_cast<size_t>(std::numeric_limits<int>::max())
        || input.surface_ids.size() != input.surface_indices.size() / 3
        || input.obstacle_surface_ids.size() != input.obstacle_indices.size() / 3
        || input.obstacle_owner.size() != input.obstacle_indices.size() / 3
        || input.surface_indices.size() % 3 != 0
        || input.obstacle_indices.size() % 3 != 0
        || input.surface_vertices.size()
            > static_cast<size_t>(std::numeric_limits<int>::max())
        || input.obstacle_vertices.size()
            > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return stats;
    }

    auto& scratch = g_scratch;
    scratch.surface_vertices.clear();
    scratch.surface_indices.clear();
    scratch.surface_areas.clear();
    scratch.obstacle_vertices.clear();
    scratch.obstacle_indices.clear();
    scratch.obstacle_areas.clear();
    scratch.link_vertices.clear();
    scratch.link_radii.clear();
    scratch.link_directions.clear();
    scratch.link_areas.clear();
    scratch.link_flags.clear();
    scratch.link_user_ids.clear();

    const int border_size = static_cast<int>(config.erosion_cells) + 3;
    const float border = static_cast<float>(border_size) * config.cell_size;
    std::array<float, 3> minimum{
        static_cast<float>(input.tile.x) * nav_tile_size - border,
        std::numeric_limits<float>::max(),
        static_cast<float>(input.tile.y) * nav_tile_size - border,
    };
    std::array<float, 3> maximum{
        static_cast<float>(input.tile.x + 1) * nav_tile_size + border,
        std::numeric_limits<float>::lowest(),
        static_cast<float>(input.tile.y + 1) * nav_tile_size + border,
    };

    for (uint32_t triangle : input.surface_triangles) {
        if (triangle >= input.surface_ids.size()) return stats;
        const uint32_t surface = input.surface_ids[triangle];
        if (surface >= input.surface_walkable.size()
            || input.surface_walkable[surface] == 0) {
            continue;
        }
        const size_t before = scratch.surface_areas.size();
        if (!append_triangle(input.surface_vertices, input.surface_indices,
                triangle, minimum, maximum, RC_WALKABLE_AREA,
                scratch.surface_vertices, scratch.surface_indices,
                scratch.surface_areas)) {
            return stats;
        }
        stats.rasterized_surface_triangle_count
            += scratch.surface_areas.size() != before;
    }
    for (uint32_t triangle : input.obstacle_triangles) {
        if (triangle >= input.obstacle_surface_ids.size()) return stats;
        const uint32_t owner = input.obstacle_owner[triangle];
        const uint32_t surface = input.obstacle_surface_ids[triangle];
        if (owner >= input.obstacle_active.size()) return stats;
        if (input.obstacle_active[owner] == 0
            || (surface < input.surface_walkable.size()
                && input.surface_walkable[surface] != 0)) {
            continue;
        }
        const size_t before = scratch.obstacle_areas.size();
        if (!append_triangle(input.obstacle_vertices, input.obstacle_indices,
                triangle, minimum, maximum, RC_NULL_AREA,
                scratch.obstacle_vertices, scratch.obstacle_indices,
                scratch.obstacle_areas)) {
            return stats;
        }
        stats.rasterized_obstacle_triangle_count
            += scratch.obstacle_areas.size() != before;
    }
    if (scratch.surface_areas.empty()) {
        stats.status = NavStatus::ok;
        stats.build_nanoseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        return stats;
    }

    for (uint32_t link_index : input.door_link_indices) {
        if (link_index >= input.door_links.size()) return stats;
        const auto& link = input.door_links[link_index];
        if (link.active_obstacle_state >= input.obstacle_active.size()) {
            return stats;
        }
        if (input.obstacle_active[link.active_obstacle_state] == 0) continue;
        bool valid = finite(link.start) && finite(link.end)
            && std::isfinite(link.radius) && link.radius > 0.0f;
        const uint32_t user_id = door_user_id(link.door_index, link.side, valid);
        if (!valid) return stats;
        append_recast_vertex(scratch.link_vertices, link.start);
        append_recast_vertex(scratch.link_vertices, link.end);
        scratch.link_radii.push_back(link.radius);
        // Two opposite one-way rows carry side-specific door tags. A single
        // bidirectional row could not tell the interaction system which side
        // the actor approached from.
        scratch.link_directions.push_back(0);
        scratch.link_areas.push_back(1);
        scratch.link_flags.push_back(nav_walkable_flag | nav_door_link_flag);
        scratch.link_user_ids.push_back(user_id);
        ++stats.enabled_door_link_count;
    }

    for (size_t vertex = 1; vertex < scratch.surface_vertices.size(); vertex += 3) {
        minimum[1] = std::min(minimum[1], scratch.surface_vertices[vertex]);
        maximum[1] = std::max(maximum[1], scratch.surface_vertices[vertex]);
    }
    for (size_t vertex = 1; vertex < scratch.obstacle_vertices.size(); vertex += 3) {
        minimum[1] = std::min(minimum[1], scratch.obstacle_vertices[vertex]);
        maximum[1] = std::max(maximum[1], scratch.obstacle_vertices[vertex]);
    }
    if (!std::isfinite(minimum[1]) || !std::isfinite(maximum[1])) return stats;
    minimum[1] = std::floor(minimum[1] / config.cell_height) * config.cell_height;
    maximum[1] = std::ceil(maximum[1] / config.cell_height) * config.cell_height
        + config.agent_height + config.agent_max_climb;

    rcConfig cfg{};
    cfg.cs = config.cell_size;
    cfg.ch = config.cell_height;
    cfg.walkableHeight = static_cast<int>(std::ceil(config.agent_height / cfg.ch));
    cfg.walkableClimb = static_cast<int>(std::floor(config.agent_max_climb / cfg.ch));
    cfg.walkableRadius = static_cast<int>(config.erosion_cells);
    cfg.maxEdgeLen = config.max_edge_length > 0.0f
        ? static_cast<int>(config.max_edge_length / cfg.cs)
        : 0;
    cfg.maxSimplificationError = config.max_simplification_error;
    cfg.minRegionArea = 0;
    cfg.mergeRegionArea = 0;
    cfg.maxVertsPerPoly = config.verts_per_polygon;
    cfg.detailSampleDist = config.detail_sample_distance < 0.9f
        ? 0.0f
        : cfg.cs * config.detail_sample_distance;
    cfg.detailSampleMaxError = cfg.ch * config.detail_sample_max_error;
    cfg.borderSize = border_size;
    std::copy(minimum.begin(), minimum.end(), cfg.bmin);
    std::copy(maximum.begin(), maximum.end(), cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
    if (cfg.width <= 0 || cfg.height <= 0) return stats;

    SilentRecastContext context;
    RecastHandle<rcHeightfield, rcFreeHeightField> solid{rcAllocHeightfield()};
    if (!solid.get() || !rcCreateHeightfield(&context, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        return stats;
    }
    if (!rcRasterizeTriangles(&context, scratch.surface_vertices.data(),
            static_cast<int>(scratch.surface_vertices.size() / 3),
            scratch.surface_indices.data(), scratch.surface_areas.data(),
            static_cast<int>(scratch.surface_areas.size()), *solid,
            cfg.walkableClimb)) {
        return stats;
    }
    if (!scratch.obstacle_areas.empty()
        && !rcRasterizeTriangles(&context, scratch.obstacle_vertices.data(),
            static_cast<int>(scratch.obstacle_vertices.size() / 3),
            scratch.obstacle_indices.data(), scratch.obstacle_areas.data(),
            static_cast<int>(scratch.obstacle_areas.size()), *solid,
            cfg.walkableClimb)) {
        return stats;
    }
    // WOK material already classifies every authored surface span. The
    // low-hanging recovery pass exists for slope-derived walkability and can
    // incorrectly recover an explicitly null obstacle span, so it is not part
    // of this authored-material pipeline.
    rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid);

    RecastHandle<rcCompactHeightfield, rcFreeCompactHeightfield> compact{
        rcAllocCompactHeightfield()};
    if (!compact.get()
        || !rcBuildCompactHeightfield(&context, cfg.walkableHeight,
            cfg.walkableClimb, *solid, *compact)) {
        return stats;
    }
    stats.compact_span_count = static_cast<size_t>(compact.get()->spanCount);
    if (cfg.walkableRadius > 0
        && !rcErodeWalkableArea(&context, cfg.walkableRadius, *compact)) {
        return stats;
    }
    if (!rcBuildLayerRegions(&context, *compact, cfg.borderSize,
            cfg.minRegionArea)) {
        return stats;
    }

    RecastHandle<rcContourSet, rcFreeContourSet> contours{rcAllocContourSet()};
    if (!contours.get()
        || !rcBuildContours(&context, *compact, cfg.maxSimplificationError,
            cfg.maxEdgeLen, *contours)) {
        return stats;
    }
    stats.contour_count = static_cast<size_t>(contours.get()->nconts);
    if (contours.get()->nconts == 0) {
        stats.status = NavStatus::ok;
        stats.build_nanoseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        return stats;
    }

    RecastHandle<rcPolyMesh, rcFreePolyMesh> poly_mesh{rcAllocPolyMesh()};
    if (!poly_mesh.get()
        || !rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly,
            *poly_mesh)) {
        return stats;
    }
    RecastHandle<rcPolyMeshDetail, rcFreePolyMeshDetail> detail_mesh{
        rcAllocPolyMeshDetail()};
    if (!detail_mesh.get()
        || !rcBuildPolyMeshDetail(&context, *poly_mesh, *compact,
            cfg.detailSampleDist, cfg.detailSampleMaxError, *detail_mesh)) {
        return stats;
    }
    if (!normalize_authored_heights(*poly_mesh, *detail_mesh,
            scratch.surface_vertices, config.cell_height,
            stats.normalized_height_vertex_count)) {
        return stats;
    }
    for (int vertex = 0; vertex < detail_mesh.get()->nverts; ++vertex) {
        const float height = detail_mesh.get()->verts[vertex * 3 + 1];
        if (!std::isfinite(height)
            || height < minimum[1] - config.cell_height
            || height > maximum[1] + config.cell_height) {
            return stats;
        }
    }
    if (poly_mesh.get()->nverts >= 0xffff) return stats;

    for (int polygon = 0; polygon < poly_mesh.get()->npolys; ++polygon) {
        poly_mesh.get()->areas[polygon] = 1;
        poly_mesh.get()->flags[polygon] = nav_walkable_flag;
    }

    dtNavMeshCreateParams params{};
    params.verts = poly_mesh.get()->verts;
    params.vertCount = poly_mesh.get()->nverts;
    params.polys = poly_mesh.get()->polys;
    params.polyAreas = poly_mesh.get()->areas;
    params.polyFlags = poly_mesh.get()->flags;
    params.polyCount = poly_mesh.get()->npolys;
    params.nvp = poly_mesh.get()->nvp;
    params.detailMeshes = detail_mesh.get()->meshes;
    params.detailVerts = detail_mesh.get()->verts;
    params.detailVertsCount = detail_mesh.get()->nverts;
    params.detailTris = detail_mesh.get()->tris;
    params.detailTriCount = detail_mesh.get()->ntris;
    params.offMeshConVerts = scratch.link_vertices.data();
    params.offMeshConRad = scratch.link_radii.data();
    params.offMeshConDir = scratch.link_directions.data();
    params.offMeshConAreas = scratch.link_areas.data();
    params.offMeshConFlags = scratch.link_flags.data();
    params.offMeshConUserID = scratch.link_user_ids.data();
    params.offMeshConCount = static_cast<int>(scratch.link_radii.size());
    params.walkableHeight = config.agent_height;
    params.walkableRadius = static_cast<float>(config.erosion_cells) * config.cell_size;
    params.walkableClimb = config.agent_max_climb;
    params.tileX = static_cast<int>(input.tile.x);
    params.tileY = static_cast<int>(input.tile.y);
    params.tileLayer = 0;
    std::memcpy(params.bmin, poly_mesh.get()->bmin, sizeof(params.bmin));
    std::memcpy(params.bmax, poly_mesh.get()->bmax, sizeof(params.bmax));
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* payload = nullptr;
    int payload_size = 0;
    if (!dtCreateNavMeshData(&params, &payload, &payload_size)
        || !payload || payload_size <= 0) {
        dtFree(payload);
        return stats;
    }
    output.data_ = payload;
    output.size_ = payload_size;
    stats.polygon_count = static_cast<size_t>(poly_mesh.get()->npolys);
    stats.vertex_count = static_cast<size_t>(poly_mesh.get()->nverts);
    stats.detail_triangle_count = static_cast<size_t>(detail_mesh.get()->ntris);
    stats.payload_bytes = static_cast<size_t>(payload_size);
    stats.status = NavStatus::ok;
    stats.build_nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return stats;
}

} // namespace nw::nav
