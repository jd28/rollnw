#pragma once

#include "../util/ByteArray.hpp"

#include <filesystem>

namespace nw {

struct PltLayer {
    static constexpr size_t skin = 0;
    static constexpr size_t hair = 1;
    static constexpr size_t metal1 = 2;
    static constexpr size_t metal2 = 3;
    static constexpr size_t cloth1 = 4;
    static constexpr size_t cloth2 = 5;
    static constexpr size_t leather1 = 6;
    static constexpr size_t leather2 = 7;
    static constexpr size_t tattoo1 = 8;
    static constexpr size_t tattoo2 = 9;

    static constexpr size_t size = 10;
};

struct PltPixel {
    uint8_t color;
    uint8_t layer;
};

struct Plt {
    Plt(std::filesystem::path file);
    Plt(ByteArray bytes);

    uint32_t height() const;
    const PltPixel* pixels() const;
    bool valid() const;
    uint32_t width() const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    ByteArray bytes_;
    bool valid_ = false;
};

} // namespace nw
