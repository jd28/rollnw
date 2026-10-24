#include "model_texture_cache.hpp"

#include <absl/hash/hash.h>

#include <algorithm>
#include <string_view>

namespace nw::render {

namespace {

size_t texture_hash(std::span<const uint8_t> bytes, nw::ResourceType::type source_type,
    nw::gfx::Fmt format, bool premultiply_alpha)
{
    return absl::HashOf(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()},
        source_type, format, premultiply_alpha);
}

} // namespace

ModelTexture::~ModelTexture()
{
    if (context_ && texture.valid()) {
        nw::gfx::destroy_texture(context_, texture);
    }
}

std::shared_ptr<const ModelTexture> ModelTextureCache::find(std::span<const uint8_t> bytes,
    nw::ResourceType::type source_type, nw::gfx::Fmt format, bool premultiply_alpha) const
{
    if (bytes.empty()) {
        return {};
    }
    const auto found = textures_.find(texture_hash(bytes, source_type, format, premultiply_alpha));
    if (found == textures_.end()) {
        return {};
    }
    for (const auto& row : found->second) {
        auto texture = row.lock();
        if (texture && texture->source_type_ == source_type && texture->format_ == format
            && texture->premultiply_alpha_ == premultiply_alpha
            && std::ranges::equal(texture->source_bytes_, bytes)) {
            return texture;
        }
    }
    return {};
}

std::shared_ptr<const ModelTexture> ModelTextureCache::adopt(std::span<const uint8_t> bytes,
    nw::ResourceType::type source_type, nw::gfx::Fmt format, bool premultiply_alpha,
    nw::gfx::Handle<nw::gfx::Texture> handle, size_t upload_bytes)
{
    if (!context_ || bytes.empty() || !handle.valid()) {
        return {};
    }
    const auto index = nw::gfx::get_bindless_texture_index(context_, handle);
    if (!nw::gfx::bindless_texture_index_valid(index)) {
        return {};
    }
    auto texture = std::shared_ptr<ModelTexture>{new ModelTexture};
    texture->source_bytes_.assign(bytes.begin(), bytes.end());
    texture->source_type_ = source_type;
    texture->format_ = format;
    texture->premultiply_alpha_ = premultiply_alpha;
    texture->upload_bytes_ = upload_bytes;
    textures_[texture_hash(bytes, source_type, format, premultiply_alpha)].push_back(texture);
    texture->context_ = context_;
    texture->texture = handle;
    texture->index = index;
    return texture;
}

void ModelTextureCache::prune()
{
    for (auto it = textures_.begin(); it != textures_.end();) {
        std::erase_if(it->second, [](const auto& row) { return row.expired(); });
        if (it->second.empty()) {
            textures_.erase(it++);
        } else {
            ++it;
        }
    }
}

void ModelTextureCache::clear()
{
    textures_.clear();
}

ModelTextureCacheStats ModelTextureCache::stats() const
{
    ModelTextureCacheStats result;
    for (const auto& [hash, rows] : textures_) {
        for (const auto& row : rows) {
            if (auto texture = row.lock()) {
                ++result.live_texture_count;
                result.source_bytes += texture->source_bytes_.size();
                result.upload_bytes += texture->upload_bytes_;
            }
        }
    }
    return result;
}

} // namespace nw::render
