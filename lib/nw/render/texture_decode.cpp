#include "texture_decode.hpp"

#include <cstddef>
#include <cstdint>

namespace nw::render {

bool tga_texture_rows_need_file_order_restore(const nw::ResourceData& data) noexcept
{
    constexpr uint8_t tga_top_origin_mask = 0x20;
    constexpr size_t tga_descriptor_offset = 17;

    if (data.name.type != nw::ResourceType::tga || data.bytes.size() <= tga_descriptor_offset) {
        return false;
    }

    const auto descriptor = data.bytes[tga_descriptor_offset];
    return (descriptor & tga_top_origin_mask) == 0;
}

bool premultiply_rgba8_pixels(std::span<uint8_t> pixels) noexcept
{
    constexpr size_t channels = 4;
    if (pixels.size() % channels != 0) {
        return false;
    }

    for (size_t i = 0; i < pixels.size(); i += channels) {
        const uint8_t alpha = pixels[i + 3];
        pixels[i + 0] = static_cast<uint8_t>((uint16_t{pixels[i + 0]} * alpha + 127) / 255);
        pixels[i + 1] = static_cast<uint8_t>((uint16_t{pixels[i + 1]} * alpha + 127) / 255);
        pixels[i + 2] = static_cast<uint8_t>((uint16_t{pixels[i + 2]} * alpha + 127) / 255);
    }
    return true;
}

} // namespace nw::render
