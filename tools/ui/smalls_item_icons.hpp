#pragma once

#include "rml_generated_texture.hpp"

#include <nw/objects/ObjectHandle.hpp>

#include <span>
#include <string>
#include <vector>

namespace nw::toolset {

struct ItemIconTextureCache {
    uint64_t resource_generation = 0;
    std::vector<RmlGeneratedTexture> textures;
};

// Sources are aligned one-for-one with items. Empty sources mean the
// materialized Smalls protocol or its resources could not provide an icon.
struct ItemIconBatch {
    std::vector<std::string> sources;
    uint32_t missing_count = 0;
    bool protocol_valid = true;
    std::string diagnostic;
};

void build_item_icon_images(
    uint8_t variant,
    std::span<const ObjectHandle> items,
    ItemIconTextureCache& cache,
    ItemIconBatch& output);

} // namespace nw::toolset
