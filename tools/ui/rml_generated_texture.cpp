#include "rml_generated_texture.hpp"

#include <nw/formats/Image.hpp>

#include <algorithm>
#include <limits>

namespace nw::toolset {

bool copy_image_rgba(
    const nw::Image& image, bool flip_rows, RmlGeneratedTexture& output)
{
    if (!image.valid() || image.width() == 0 || image.height() == 0) {
        return false;
    }
    const size_t pixels = static_cast<size_t>(image.width()) * image.height();
    if (pixels > std::numeric_limits<size_t>::max() / 4) {
        return false;
    }
    const uint32_t channels = image.channels();
    if (channels == 0 || channels > 4) {
        return false;
    }

    output.width = image.width();
    output.height = image.height();
    output.rgba.resize(pixels * 4);
    const auto* source = image.data();
    for (uint32_t y = 0; y < image.height(); ++y) {
        const uint32_t source_y = flip_rows ? image.height() - y - 1 : y;
        for (uint32_t x = 0; x < image.width(); ++x) {
            const size_t source_index
                = (static_cast<size_t>(source_y) * image.width() + x) * channels;
            const size_t output_index
                = (static_cast<size_t>(y) * image.width() + x) * 4;
            if (channels == 1 || channels == 2) {
                output.rgba[output_index + 0] = source[source_index];
                output.rgba[output_index + 1] = source[source_index];
                output.rgba[output_index + 2] = source[source_index];
                output.rgba[output_index + 3]
                    = channels == 2 ? source[source_index + 1] : 255;
            } else {
                output.rgba[output_index + 0] = source[source_index + 0];
                output.rgba[output_index + 1] = source[source_index + 1];
                output.rgba[output_index + 2] = source[source_index + 2];
                output.rgba[output_index + 3]
                    = channels == 4 ? source[source_index + 3] : 255;
            }
        }
    }
    return true;
}

const RmlGeneratedTexture* find_generated_texture(
    std::span<const RmlGeneratedTexture> textures, std::string_view source) noexcept
{
    std::string normalized{source};
    std::replace(normalized.begin(), normalized.end(), '|', ':');
    const auto row = std::lower_bound(textures.begin(), textures.end(), normalized,
        [](const RmlGeneratedTexture& texture, std::string_view key) {
            return texture.source < key;
        });
    return row != textures.end() && row->source == normalized ? &*row : nullptr;
}

} // namespace nw::toolset
