#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {

// Cold, main-thread protocol for pixels generated from live authoring data.
// The producer owns every byte; RmlUi consumes a row synchronously in
// LoadTexture and uploads an independent GPU texture.
struct RmlGeneratedTexture {
    std::string source;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t visible_x = 0;
    uint32_t visible_y = 0;
    uint32_t visible_width = 0;
    uint32_t visible_height = 0;
    std::vector<uint8_t> rgba;
};

[[nodiscard]] const RmlGeneratedTexture* find_generated_texture(
    std::span<const RmlGeneratedTexture> textures, std::string_view source) noexcept;

} // namespace nw::toolset
