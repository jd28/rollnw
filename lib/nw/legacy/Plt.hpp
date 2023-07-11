#pragma once

#include "../resources/ResourceData.hpp"

#include <glm/glm.hpp>

#include <array>
#include <filesystem>

namespace nw {

/// Plt formats respective layers
enum PltLayer : uint8_t {
    plt_layer_skin = 0,
    plt_layer_hair = 1,
    plt_layer_metal1 = 2,
    plt_layer_metal2 = 3,
    plt_layer_cloth1 = 4,
    plt_layer_cloth2 = 5,
    plt_layer_leather1 = 6,
    plt_layer_leather2 = 7,
    plt_layer_tattoo1 = 8,
    plt_layer_tattoo2 = 9,

    plt_layer_size = 10,
};

/// Plt Pixel
struct PltPixel {
    uint8_t color;
    PltLayer layer;
};

/// Plt Color Array
/// @note This would be the colors that a player would select
struct PltColors {
    std::array<uint8_t, plt_layer_size> data{};
};

/// Implementation of Bioware's PLT file format
struct Plt {
    Plt(std::filesystem::path file);
    Plt(ResourceData data);

    /// Gets height
    uint32_t height() const;

    /// Gets pixel array
    const PltPixel* pixels() const;

    /// Determines if PLT was successfully parsed
    bool valid() const;

    /// Gets width
    uint32_t width() const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    ResourceData data_;
    bool valid_ = false;
};

/// Decodes PLT and user selected colors to RBGA
std::array<uint8_t, 4> decode_plt_color(const Plt& plt, const PltColors& colors, uint32_t x, uint32_t y);

} // namespace nw
