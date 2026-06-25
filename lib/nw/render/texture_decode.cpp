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

} // namespace nw::render
