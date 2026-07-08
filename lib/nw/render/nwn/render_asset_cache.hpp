#pragma once

#include "model_loader.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <memory>
#include <string>

namespace nw::render::nwn {

struct RenderAssetCacheStats {
    size_t texture_count = 0;
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
    explicit RenderAssetCache(nw::gfx::Context* ctx)
        : ctx_{ctx}
    {
    }

    void clear(nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    void destroy(nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    [[nodiscard]] RenderAssetCacheStats stats() const noexcept;

    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(
        const std::string& name, bool premultiply_alpha,
        nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(
        const std::string& name, const nw::PltColors& colors, bool premultiply_alpha,
        nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_linear_texture(const std::string& name);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_roughness_surface_texture(const std::string& name);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_raw_plt_texture(const std::string& name);
    const nw::Image* get_or_load_source_image(const std::string& name);
    bool source_image_is_white_alpha_mask(const std::string& name);
    [[nodiscard]] bool texture_rows_flipped_on_upload(const std::string& name, bool premultiply_alpha) const;
    ModelInstance* get_or_load_particle_mesh(const std::string& resref);

private:
    struct CachedTexture {
        nw::gfx::Handle<nw::gfx::Texture> texture;
        size_t upload_bytes = 0;
        bool rows_flipped = false;
    };

    nw::gfx::Context* ctx_ = nullptr;
    absl::flat_hash_map<std::string, CachedTexture> texture_cache_;
    absl::flat_hash_map<std::string, std::unique_ptr<nw::Image>> image_cache_;
    absl::flat_hash_map<std::string, bool> white_alpha_mask_cache_;
    absl::flat_hash_map<std::string, std::unique_ptr<ModelInstance>> particle_mesh_cache_;
};

} // namespace nw::render::nwn
