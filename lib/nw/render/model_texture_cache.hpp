#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/resources/assets.hpp>

#include <absl/container/flat_hash_map.h>

#include <memory>
#include <span>
#include <vector>

namespace nw::render {

// Immutable ownership record, retained by models rather than the lookup cache.
// The context must outlive all owners. Release owners only after GPU use ends,
// just as for RenderModel's directly owned textures and buffers.
struct ModelTexture {
    ~ModelTexture();
    ModelTexture(const ModelTexture&) = delete;
    ModelTexture& operator=(const ModelTexture&) = delete;

    nw::gfx::Handle<nw::gfx::Texture> texture;
    nw::gfx::BindlessTextureIndex index = nw::gfx::kInvalidBindlessTextureIndex;

private:
    friend class ModelTextureCache;
    ModelTexture() = default;
    nw::gfx::Context* context_ = nullptr;
    std::vector<uint8_t> source_bytes_;
    nw::ResourceType::type source_type_ = nw::ResourceType::invalid;
    nw::gfx::Fmt format_ = nw::gfx::Fmt::RGBA8;
    bool premultiply_alpha_ = false;
    size_t upload_bytes_ = 0;
};

struct ModelTextureCacheStats {
    size_t live_texture_count = 0;
    size_t source_bytes = 0;
    size_t upload_bytes = 0; // Base-level RGBA bytes, excluding generated mips.
};

// Render-thread-only lookup for encoded sources. Identity is the full payload,
// effective decoder type, GPU format and alpha policy; names are not identity.
// Clearing/pruning never invalidates textures retained by live models.
class ModelTextureCache {
public:
    explicit ModelTextureCache(nw::gfx::Context* context)
        : context_{context}
    {
    }

    [[nodiscard]] nw::gfx::Context* context() const noexcept { return context_; }
    [[nodiscard]] std::shared_ptr<const ModelTexture> find(std::span<const uint8_t> bytes,
        nw::ResourceType::type source_type, nw::gfx::Fmt format, bool premultiply_alpha) const;
    // Adopts a successfully uploaded texture from this cache's context. Invalid
    // handles/empty payloads are rejected without taking ownership.
    [[nodiscard]] std::shared_ptr<const ModelTexture> adopt(std::span<const uint8_t> bytes,
        nw::ResourceType::type source_type, nw::gfx::Fmt format, bool premultiply_alpha,
        nw::gfx::Handle<nw::gfx::Texture> texture, size_t upload_bytes);
    void prune();
    void clear();
    [[nodiscard]] ModelTextureCacheStats stats() const;

private:
    nw::gfx::Context* context_;
    absl::flat_hash_map<size_t, std::vector<std::weak_ptr<const ModelTexture>>> textures_;
};

} // namespace nw::render
