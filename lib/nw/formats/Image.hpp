#pragma once

#include "../resources/assets.hpp"

#include <filesystem>

namespace nw {

struct Plt;
struct PltColors;

/**
 * @brief Image Resource
 *
 * Read/Write Support:
 * - jpg, png, dds, tga (thanks to stb_image and SOIL)
 * - Bioware dds (thanks to NWNExplorer)
 *
 * @note Even though this supports writing images, this is **catagorically**
 *       not a tool for converting/compressing textures.
 */
struct Image {
    Image() = default;
    explicit Image(const Plt& plt, const PltColors& colors);
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
    uint8_t* data() const noexcept;

    /// Get height
    uint32_t height() const noexcept;

    /// Returns true if image was loaded from a bioware dds file
    bool is_bio_dds() const noexcept;

    /// Releases the underlying array of bytes
    uint8_t* release();

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
    bool is_bio_dds_ = false;

    bool parse();
    bool parse_dds();
    bool parse_bioware();
    bool parse_dxt();
};

} // namespace nw
