#pragma once

#include <nw/resources/assets.hpp>

namespace nw::render {

// stb_image normalizes bottom-origin TGA images to top-row-first decoded
// memory. NWN UV-sampled textures expect the original file row order, so both
// legacy and common model upload paths restore those rows before GPU upload.
[[nodiscard]] bool tga_texture_rows_need_file_order_restore(const nw::ResourceData& data) noexcept;

} // namespace nw::render
