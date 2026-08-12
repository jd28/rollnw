#include "rml_generated_texture.hpp"

#include <algorithm>

namespace nw::toolset {

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
