#pragma once

#include "../util/ByteArray.hpp"

#include <memory>
#include <string>

namespace nw {

struct Image {
    explicit Image(const std::filesystem::path& filename);
    explicit Image(ByteArray bytes, bool is_dds = false);

    /// Get BBP
    int channels();

    /// Get raw data
    uint8_t* data();

    /// Get height
    int height() const;

    /// Determine if successfully loaded.
    bool valid() const;

    /// Get width
    int width() const;

    /**
     * @brief Write Image to file
     *
     * @note This included for utility purposes.  There is no goal of it being a worthwhile
     *       image conversion library.
     */
    bool write_to(const std::filesystem::path& filename) const;

private:
    ByteArray bytes_;
    bool is_loaded_ = false;
    std::unique_ptr<uint8_t[], void (*)(void*)> data_;
    int channels_;
    int height_;
    int width_;
    bool is_dds_ = false;

    bool parse();
    bool parse_dds();
    bool parse_bioware();
    bool parse_dxt();
};

} // namespace nw
