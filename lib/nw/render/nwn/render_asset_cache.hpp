#pragma once

#include "model_loader.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace nw::render::nwn {

class RenderAssetCache {
public:
    explicit RenderAssetCache(nw::gfx::Context* ctx)
        : ctx_{ctx}
    {
    }

    void destroy(nw::gfx::Handle<nw::gfx::Texture> fallback_texture);

    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(
        const std::string& name, bool premultiply_alpha,
        nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_texture(
        const std::string& name, const nw::PltColors& colors, bool premultiply_alpha,
        nw::gfx::Handle<nw::gfx::Texture> fallback_texture);
    nw::gfx::Handle<nw::gfx::Texture> get_or_load_raw_plt_texture(const std::string& name);
    const nw::Image* get_or_load_source_image(const std::string& name);
    ModelInstance* get_or_load_particle_mesh(const std::string& resref);

private:
    nw::gfx::Context* ctx_ = nullptr;
    std::unordered_map<std::string, nw::gfx::Handle<nw::gfx::Texture>> texture_cache_;
    std::unordered_map<std::string, std::unique_ptr<nw::Image>> image_cache_;
    std::unordered_map<std::string, std::unique_ptr<ModelInstance>> particle_mesh_cache_;
};

} // namespace nw::render::nwn
