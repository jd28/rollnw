#pragma once

#include "model_loader.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace nw::render::nwn {

struct RenderAssetCacheStats {
    uint64_t epoch = 0;
    uint64_t invalidation_count = 0;
    uint64_t observed_resource_generation = 0;
    uint64_t current_resource_generation = 0;
    size_t texture_count = 0;
    size_t owned_texture_count = 0;
    size_t texture_upload_bytes = 0;
    size_t source_image_count = 0;
    size_t source_image_pixel_bytes = 0;
    size_t particle_mesh_count = 0;
    size_t particle_mesh_payload_bytes = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return texture_count == 0 && texture_upload_bytes == 0
            && source_image_count == 0 && source_image_pixel_bytes == 0
            && particle_mesh_count == 0 && particle_mesh_payload_bytes == 0;
    }
};

class RenderAssetCache {
public:
    explicit RenderAssetCache(nw::gfx::Context* ctx);

    // Cache mutation is serialized on the render thread. Clear waits for GPU
    // idle, destroys cache-owned textures, drops CPU payloads, and advances the
    // epoch so retained consumers can invalidate captured handles.
    void clear();
    void sync_resource_generation();
    [[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] RenderAssetCacheStats stats() const noexcept;

    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(
        nw::Resref name, bool premultiply_alpha,
        nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(
        nw::Resref name, const nw::PltColors& colors, bool premultiply_alpha,
        nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_linear_texture(nw::Resref name);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_roughness_surface_texture(nw::Resref name);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_raw_plt_texture(nw::Resref name);
    // These pointers borrow cache-owned storage. They are frame-local protocol
    // values and must not be retained across clear() or a resource reload.
    const nw::Image* get_or_load_source_image(nw::Resref name);
    bool source_image_is_white_alpha_mask(nw::Resref name);
    [[nodiscard]] bool texture_rows_flipped_on_upload(nw::Resref name, bool premultiply_alpha);
    ModelInstance* get_or_load_particle_mesh(nw::Resref resref);

private:
    enum class TextureVariant : uint8_t {
        srgb_straight,
        srgb_premultiplied,
        linear,
        roughness_surface,
        plt_straight,
        plt_premultiplied,
        raw_plt,
    };

    struct TextureCacheKey {
        nw::Resref resref;
        nw::PltColors plt_colors;
        TextureVariant variant = TextureVariant::srgb_straight;

        bool operator==(const TextureCacheKey& other) const noexcept
        {
            return resref == other.resref
                && plt_colors.data == other.plt_colors.data
                && variant == other.variant;
        }

        template <typename H>
        friend H AbslHashValue(H h, const TextureCacheKey& key)
        {
            h = H::combine(std::move(h), key.resref, key.variant);
            return H::combine_contiguous(
                std::move(h), key.plt_colors.data.data(), key.plt_colors.data.size());
        }
    };

    struct CachedTexture {
        nw::gfx::Handle<nw::gfx::Texture> texture;
        size_t upload_bytes = 0;
        bool rows_flipped = false;
        bool owned = false;
    };

    nw::gfx::Context* ctx_ = nullptr;
    uint64_t epoch_ = 1;
    uint64_t invalidation_count_ = 0;
    uint64_t observed_resource_generation_ = 0;
    absl::flat_hash_map<TextureCacheKey, CachedTexture> texture_cache_;
    absl::flat_hash_map<nw::Resref, std::unique_ptr<nw::Image>> image_cache_;
    absl::flat_hash_map<nw::Resref, bool> white_alpha_mask_cache_;
    absl::flat_hash_map<nw::Resref, std::unique_ptr<ModelInstance>> particle_mesh_cache_;
};

} // namespace nw::render::nwn
