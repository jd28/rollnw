#pragma once

#include "../resources/ResourceData.hpp"

#include <memory>
#include <string>

namespace nw {

/**
 * @brief Image Resource
 *
 * Read/Write Support:
 * - jpg, png, dds, tga (thanks to stb_image and SOIL)
 * - Bioware dds (thanks to NWNExplorer)
 *
 * @note Even though this supports writing images, this is **catagorically**
 *       not a tool for converting/compressing textures.
 * @todo plt
 */
struct Image {
    explicit Image(const std::filesystem::path& filename);
    explicit Image(ResourceData data);

    Image(Image&& other);
    Image(const Image& other) = delete;
    Image& operator=(Image&& other);
    Image& operator=(const Image& other) = delete;
    ~Image();

    /// Get BBP
    uint32_t channels() const noexcept;

    /// Get raw data
    uint8_t* data();

    /// Get height
    uint32_t height() const noexcept;

    /// Determine if successfully loaded.
    bool valid() const;

    /// Get width
    uint32_t width() const noexcept;

    /// Write Image to file
    bool write_to(const std::filesystem::path& filename) const;

private:
    ResourceData data_;
    bool is_loaded_ = false;
    uint8_t* bytes_ = nullptr;
    size_t size_ = 0;
    uint32_t channels_ = 0;
    uint32_t height_ = 0;
    uint32_t width_ = 0;
    bool is_dds_ = false;

    bool parse();
    bool parse_dds();
    bool parse_bioware();
    bool parse_dxt();
};

} // namespace nw
