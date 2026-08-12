#pragma once

#include <nw/resources/assets.hpp>

#include <span>

namespace nw::render {

// stb_image normalizes bottom-origin TGA images to top-row-first decoded
// memory. NWN UV-sampled textures expect the original file row order, so image
// upload consumers restore those rows before GPU upload.
[[nodiscard]] bool tga_texture_rows_need_file_order_restore(const nw::ResourceData& data) noexcept;

// Converts a complete batch of interleaved RGBA8 pixels from straight to
// premultiplied alpha. Rejects incomplete trailing pixels without modifying
// the input.
[[nodiscard]] bool premultiply_rgba8_pixels(std::span<uint8_t> pixels) noexcept;

} // namespace nw::render
