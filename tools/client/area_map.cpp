#include "area_map.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Ini.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/platform.hpp>

#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace nw::toolset {

namespace {

constexpr std::string_view kAreaMapCacheDirectory = ".rollnw/cache/area_maps";

struct CachedTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

struct AreaMapCache {
    std::unordered_map<std::string, size_t> tileset_indices;
    std::vector<std::unique_ptr<nw::Ini>> tilesets;
    std::unordered_map<std::string, size_t> texture_indices;
    std::unordered_set<std::string> missing_textures;
    std::vector<CachedTexture> textures;
};

bool checked_pixel_count(uint32_t width, uint32_t height, size_t& count) noexcept
{
    if (width == 0 || height == 0
        || width > kAreaMapMaxDimension || height > kAreaMapMaxDimension) {
        return false;
    }
    const uint64_t result = static_cast<uint64_t>(width) * height;
    if (result > std::numeric_limits<size_t>::max() / 4) {
        return false;
    }
    count = static_cast<size_t>(result);
    return true;
}

const nw::Ini* load_tileset(AreaMapCache& cache, std::string_view resref, std::string& error)
{
    const std::string key{resref};
    if (const auto found = cache.tileset_indices.find(key); found != cache.tileset_indices.end()) {
        return cache.tilesets[found->second].get();
    }

    auto data = nw::kernel::resman().demand({nw::Resref{resref}, nw::ResourceType::set});
    auto ini = std::make_unique<nw::Ini>(std::move(data));
    if (!ini->valid()) {
        error = "failed to load tileset '" + key + "'";
        return nullptr;
    }

    const size_t index = cache.tilesets.size();
    cache.tileset_indices.emplace(key, index);
    cache.tilesets.push_back(std::move(ini));
    return cache.tilesets.back().get();
}

bool convert_to_rgba(const nw::Image& image, CachedTexture& out)
{
    if (!image.valid() || image.width() == 0 || image.height() == 0) {
        return false;
    }
    const uint32_t channels = image.channels();
    if (channels < 1 || channels > 4) {
        return false;
    }

    size_t pixel_count = 0;
    if (!checked_pixel_count(image.width(), image.height(), pixel_count)) {
        return false;
    }
    out.width = image.width();
    out.height = image.height();
    out.rgba.resize(pixel_count * 4);

    const uint8_t* input = image.data();
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint8_t* src = input + i * channels;
        uint8_t* dst = out.rgba.data() + i * 4;
        if (channels <= 2) {
            dst[0] = src[0];
            dst[1] = src[0];
            dst[2] = src[0];
            dst[3] = channels == 2 ? src[1] : 255;
        } else {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = channels == 4 ? src[3] : 255;
        }
    }
    return true;
}

const CachedTexture* load_texture(AreaMapCache& cache, std::string_view resref, std::string& error)
{
    const std::string key{resref};
    if (const auto found = cache.texture_indices.find(key); found != cache.texture_indices.end()) {
        return &cache.textures[found->second];
    }
    if (cache.missing_textures.contains(key)) {
        error = "failed to load map texture '" + key + "'";
        return nullptr;
    }

    std::unique_ptr<nw::Image> image{nw::kernel::resman().texture(nw::Resref{resref})};
    CachedTexture texture;
    if (!image || !convert_to_rgba(*image, texture)) {
        cache.missing_textures.emplace(key);
        error = "failed to load map texture '" + key + "'";
        return nullptr;
    }

    const size_t index = cache.textures.size();
    cache.texture_indices.emplace(key, index);
    cache.textures.push_back(std::move(texture));
    return &cache.textures.back();
}

void fill_missing_tile(uint32_t tile_pixels,
    uint32_t output_x,
    uint32_t output_y,
    uint32_t output_width,
    std::vector<uint8_t>& output)
{
    constexpr std::array<uint8_t, 4> dark{32, 35, 40, 255};
    constexpr std::array<uint8_t, 4> light{122, 126, 134, 255};
    const uint32_t cell_pixels = std::max(1u, tile_pixels / 4);
    for (uint32_t y = 0; y < tile_pixels; ++y) {
        for (uint32_t x = 0; x < tile_pixels; ++x) {
            const auto& color = ((x / cell_pixels) + (y / cell_pixels)) & 1 ? light : dark;
            const size_t output_offset = (static_cast<size_t>(output_y + y) * output_width
                                             + output_x + x)
                * 4;
            std::copy(color.begin(), color.end(), output.data() + output_offset);
        }
    }
}

template <typename CoordinateTransform>
void copy_resampled_tile(const CachedTexture& source,
    uint32_t output_tile_pixels,
    uint32_t output_x,
    uint32_t output_y,
    uint32_t output_width,
    std::vector<uint8_t>& output,
    CoordinateTransform transform)
{
    for (uint32_t y = 0; y < output_tile_pixels; ++y) {
        for (uint32_t x = 0; x < output_tile_pixels; ++x) {
            const auto [oriented_x, oriented_y] = transform(x, y, output_tile_pixels);
            const uint32_t source_x = std::min(source.width - 1,
                static_cast<uint32_t>((static_cast<uint64_t>(oriented_x) * source.width)
                    / output_tile_pixels));
            const uint32_t source_y = std::min(source.height - 1,
                static_cast<uint32_t>((static_cast<uint64_t>(oriented_y) * source.height)
                    / output_tile_pixels));
            const size_t source_offset = (static_cast<size_t>(source_y) * source.width + source_x) * 4;
            const size_t output_offset = (static_cast<size_t>(output_y + y) * output_width
                                             + output_x + x)
                * 4;
            std::copy_n(source.rgba.data() + source_offset, 4, output.data() + output_offset);
        }
    }
}

void copy_oriented_tile(const CachedTexture& source,
    int32_t orientation,
    uint32_t output_tile_pixels,
    uint32_t output_x,
    uint32_t output_y,
    uint32_t output_width,
    std::vector<uint8_t>& output)
{
    switch (orientation & 3) {
    case 1:
        copy_resampled_tile(source, output_tile_pixels, output_x, output_y, output_width, output,
            [](uint32_t x, uint32_t y, uint32_t extent) {
                return std::pair{extent - 1 - y, x};
            });
        break;
    case 2:
        copy_resampled_tile(source, output_tile_pixels, output_x, output_y, output_width, output,
            [](uint32_t x, uint32_t y, uint32_t extent) {
                return std::pair{extent - 1 - x, extent - 1 - y};
            });
        break;
    case 3:
        copy_resampled_tile(source, output_tile_pixels, output_x, output_y, output_width, output,
            [](uint32_t x, uint32_t y, uint32_t extent) {
                return std::pair{y, extent - 1 - x};
            });
        break;
    default:
        copy_resampled_tile(source, output_tile_pixels, output_x, output_y, output_width, output,
            [](uint32_t x, uint32_t y, uint32_t) {
                return std::pair{x, y};
            });
        break;
    }
}

bool compose_area_map(const AreaMapSource& source,
    AreaMapCache& cache,
    uint32_t& output_width,
    uint32_t& output_height,
    std::vector<uint8_t>& output,
    bool& degraded,
    std::string& warning,
    std::string& error)
{
    const auto output_size = area_map_output_size(source.width, source.height);
    if (!output_size) {
        error = "area '" + source.resref + "' dimensions are outside the supported map range";
        return false;
    }

    const uint64_t expected_tiles = static_cast<uint64_t>(source.width) * source.height;
    if (expected_tiles != source.tiles.size()) {
        error = "area '" + source.resref + "' tile count does not match its dimensions";
        return false;
    }
    const uint32_t tile_pixels = output_size->tile_pixels;
    output_width = output_size->width;
    output_height = output_size->height;
    size_t pixel_count = 0;
    if (!checked_pixel_count(output_width, output_height, pixel_count)) {
        error = "area '" + source.resref + "' map size is outside the supported range";
        return false;
    }

    const nw::Ini* tileset = load_tileset(cache, source.tileset, error);
    if (!tileset) {
        error = "area '" + source.resref + "': " + error;
        return false;
    }

    output.assign(pixel_count * 4, 0);
    degraded = false;
    for (size_t tile_index = 0; tile_index < source.tiles.size(); ++tile_index) {
        const auto& tile = source.tiles[tile_index];
        if (tile.id < 0) {
            error = "area '" + source.resref + "' contains a negative tile id";
            return false;
        }

        const uint32_t source_row = static_cast<uint32_t>(tile_index) / static_cast<uint32_t>(source.width);
        const uint32_t source_column = static_cast<uint32_t>(tile_index) % static_cast<uint32_t>(source.width);
        const uint32_t output_row = static_cast<uint32_t>(source.height) - 1 - source_row;
        const uint32_t output_x = source_column * tile_pixels;
        const uint32_t output_y = output_row * tile_pixels;

        nw::String texture_name;
        const nw::String key = "tile" + std::to_string(tile.id) + "/imagemap2d";
        if (!tileset->get_to(key, texture_name) || texture_name.empty()) {
            degraded = true;
            if (warning.empty()) {
                warning = "area '" + source.resref + "' has no ImageMap2D for tile "
                    + std::to_string(tile.id);
            }
            fill_missing_tile(tile_pixels, output_x, output_y, output_width, output);
            continue;
        }
        nw::string::tolower(&texture_name);
        const CachedTexture* texture = load_texture(cache, texture_name, error);
        if (!texture) {
            degraded = true;
            if (warning.empty()) {
                warning = "area '" + source.resref + "': " + error;
            }
            fill_missing_tile(tile_pixels, output_x, output_y, output_width, output);
            continue;
        }

        copy_oriented_tile(*texture,
            tile.orientation,
            tile_pixels,
            output_x,
            output_y,
            output_width,
            output);
    }
    return true;
}

bool write_png_atomic(const fs::path& target,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgba,
    std::string& error)
{
    fs::path temporary = target;
    temporary += ".tmp";
    const std::string temporary_string = nw::path_to_string(temporary);
    if (!stbi_write_png(temporary_string.c_str(),
            static_cast<int>(width),
            static_cast<int>(height),
            4,
            rgba.data(),
            static_cast<int>(width * 4))) {
        error = "failed to encode area map " + target.string();
        return false;
    }
    if (!nw::move_file_safely(temporary, target)) {
        std::error_code ec;
        fs::remove(temporary, ec);
        error = "failed to install area map " + target.string();
        return false;
    }
    return true;
}

} // namespace

std::optional<AreaMapOutputSize> area_map_output_size(
    int32_t area_width, int32_t area_height) noexcept
{
    if (area_width <= 0 || area_height <= 0) {
        return std::nullopt;
    }
    const uint32_t largest_axis = static_cast<uint32_t>(std::max(area_width, area_height));
    if (largest_axis > kAreaMapMaxDimension) {
        return std::nullopt;
    }
    const uint32_t tile_pixels = std::min(
        kAreaMapPreferredTilePixels, kAreaMapMaxDimension / largest_axis);
    return AreaMapOutputSize{
        .width = static_cast<uint32_t>(area_width) * tile_pixels,
        .height = static_cast<uint32_t>(area_height) * tile_pixels,
        .tile_pixels = tile_pixels,
    };
}

std::vector<AreaMapSource> collect_area_map_sources(std::span<const nw::Area* const> areas)
{
    std::vector<AreaMapSource> result;
    result.reserve(areas.size());
    for (const nw::Area* area : areas) {
        if (!area) {
            continue;
        }
        AreaMapSource source;
        source.resref = std::string{area->resref.view()};
        source.tileset = std::string{area->tileset_resref.view()};
        source.width = area->width;
        source.height = area->height;
        source.tiles.assign(area->tiles.begin(), area->tiles.end());
        result.push_back(std::move(source));
    }
    return result;
}

fs::path project_area_map_path(const fs::path& project_dir, std::string_view area_resref)
{
    return project_dir / kAreaMapCacheDirectory / (std::string{area_resref} + ".png");
}

AreaMapWriteResult write_project_area_maps(const fs::path& project_dir,
    std::span<const AreaMapSource> sources)
{
    AreaMapWriteResult result;
    if (sources.empty()) {
        return result;
    }

    const fs::path output_directory = project_dir / kAreaMapCacheDirectory;
    std::error_code ec;
    fs::create_directories(output_directory, ec);
    if (ec) {
        result.failed = sources.size();
        result.first_error = "failed to create area map directory "
            + output_directory.string() + ": " + ec.message();
        return result;
    }

    AreaMapCache cache;
    for (const auto& source : sources) {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
        bool degraded = false;
        std::string warning;
        std::string error;
        if (!compose_area_map(source, cache, width, height, rgba, degraded, warning, error)
            || !write_png_atomic(project_area_map_path(project_dir, source.resref),
                width, height, rgba, error)) {
            ++result.failed;
            if (result.first_error.empty()) {
                result.first_error = std::move(error);
            }
            continue;
        }
        ++result.written;
        if (degraded) {
            ++result.degraded;
            if (result.first_warning.empty()) {
                result.first_warning = std::move(warning);
            }
        }
    }
    return result;
}

} // namespace nw::toolset
