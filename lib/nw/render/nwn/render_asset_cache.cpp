#include "render_asset_cache.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace nw::render::nwn {

namespace {

uint32_t full_mip_count(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
        ++levels;
    }
    return levels;
}

void copy_rgba_pixels(uint8_t* dst, const uint8_t* src, size_t pixel_count, bool premultiply)
{
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint8_t a = src[i * 4 + 3];
        if (premultiply) {
            dst[i * 4 + 0] = static_cast<uint8_t>((uint16_t(src[i * 4 + 0]) * a + 127) / 255);
            dst[i * 4 + 1] = static_cast<uint8_t>((uint16_t(src[i * 4 + 1]) * a + 127) / 255);
            dst[i * 4 + 2] = static_cast<uint8_t>((uint16_t(src[i * 4 + 2]) * a + 127) / 255);
        } else {
            dst[i * 4 + 0] = src[i * 4 + 0];
            dst[i * 4 + 1] = src[i * 4 + 1];
            dst[i * 4 + 2] = src[i * 4 + 2];
        }
        dst[i * 4 + 3] = a;
    }
}

void expand_rgb_to_rgba(uint8_t* dst, const uint8_t* src, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; ++i) {
        dst[i * 4 + 0] = src[i * 3 + 0];
        dst[i * 4 + 1] = src[i * 3 + 1];
        dst[i * 4 + 2] = src[i * 3 + 2];
        dst[i * 4 + 3] = 255;
    }
}

std::string plt_cache_key(const std::string& name, const nw::PltColors& colors)
{
    std::string key = name;
    key += "#plt:";
    for (auto value : colors.data) {
        key += std::to_string(value);
        key.push_back(',');
    }
    return key;
}

std::string alpha_cache_key(const std::string& name, bool premultiply_alpha)
{
    return premultiply_alpha ? name + "#premul" : name + "#straight";
}

std::string raw_plt_cache_key(const std::string& name)
{
    return name + "#rawplt";
}

} // namespace

void RenderAssetCache::destroy(nw::gfx::Handle<nw::gfx::Texture> fallback_texture)
{
    for (auto& [name, texture] : texture_cache_) {
        if (texture.valid() && texture != fallback_texture) {
            nw::gfx::destroy_texture(ctx_, texture);
        }
    }
    texture_cache_.clear();
    image_cache_.clear();
    particle_mesh_cache_.clear();
}

nw::gfx::Handle<nw::gfx::Texture> RenderAssetCache::get_or_load_texture(
    const std::string& name, bool premultiply_alpha, nw::gfx::Handle<nw::gfx::Texture> fallback_texture)
{
    if (name.empty()) {
        return fallback_texture;
    }

    const auto cache_key = alpha_cache_key(name, premultiply_alpha);
    if (auto it = texture_cache_.find(cache_key); it != texture_cache_.end()) {
        return it->second;
    }

    auto* image = nw::kernel::resman().texture(nw::Resref(name));
    if (!image || !image->valid()) {
        LOG_F(WARNING, "Texture not found: {}", name);
        texture_cache_[cache_key] = fallback_texture;
        return fallback_texture;
    }

    nw::gfx::TextureDesc texture_desc{};
    texture_desc.width = image->width();
    texture_desc.height = image->height();
    texture_desc.mip_levels = full_mip_count(image->width(), image->height());
    texture_desc.format = nw::gfx::Fmt::RGBA8Srgb;
    auto texture = nw::gfx::create_texture(ctx_, texture_desc);
    if (!texture.valid()) {
        texture_cache_[cache_key] = fallback_texture;
        return fallback_texture;
    }

    const size_t pixel_count = static_cast<size_t>(image->width()) * image->height();
    if (image->channels() == 4) {
        std::vector<uint8_t> rgba(pixel_count * 4);
        copy_rgba_pixels(rgba.data(), image->data(), pixel_count, premultiply_alpha);
        nw::gfx::upload_texture_rgba8(ctx_, texture, rgba.data(), rgba.size());
    } else if (image->channels() == 3) {
        std::vector<uint8_t> rgba(pixel_count * 4);
        expand_rgb_to_rgba(rgba.data(), image->data(), pixel_count);
        nw::gfx::upload_texture_rgba8(ctx_, texture, rgba.data(), rgba.size());
    } else {
        LOG_F(WARNING, "Texture {} has unsupported {} channels", name, image->channels());
        nw::gfx::destroy_texture(ctx_, texture);
        texture_cache_[cache_key] = fallback_texture;
        return fallback_texture;
    }

    texture_cache_[cache_key] = texture;
    return texture;
}

nw::gfx::Handle<nw::gfx::Texture> RenderAssetCache::get_or_load_texture(
    const std::string& name, const nw::PltColors& colors, bool premultiply_alpha,
    nw::gfx::Handle<nw::gfx::Texture> fallback_texture)
{
    if (name.empty()) {
        return fallback_texture;
    }

    auto cache_key = plt_cache_key(name, colors);
    cache_key += premultiply_alpha ? "#premul" : "#straight";
    if (auto it = texture_cache_.find(cache_key); it != texture_cache_.end()) {
        return it->second;
    }

    auto plt_data = nw::kernel::resman().demand({nw::Resref{name}, nw::ResourceType::plt});
    nw::Plt plt{std::move(plt_data)};
    if (!plt.valid()) {
        return get_or_load_texture(name, premultiply_alpha, fallback_texture);
    }

    nw::Image image{plt, colors};
    if (!image.valid()) {
        texture_cache_[cache_key] = fallback_texture;
        return fallback_texture;
    }

    nw::gfx::TextureDesc texture_desc{};
    texture_desc.width = image.width();
    texture_desc.height = image.height();
    texture_desc.mip_levels = full_mip_count(image.width(), image.height());
    texture_desc.format = nw::gfx::Fmt::RGBA8Srgb;
    auto texture = nw::gfx::create_texture(ctx_, texture_desc);
    if (!texture.valid()) {
        texture_cache_[cache_key] = fallback_texture;
        return fallback_texture;
    }

    const size_t pixel_count = static_cast<size_t>(image.width()) * image.height();
    std::vector<uint8_t> rgba(pixel_count * 4);
    copy_rgba_pixels(rgba.data(), image.data(), pixel_count, premultiply_alpha);
    nw::gfx::upload_texture_rgba8(ctx_, texture, rgba.data(), rgba.size());
    texture_cache_[cache_key] = texture;
    return texture;
}

nw::gfx::Handle<nw::gfx::Texture> RenderAssetCache::get_or_load_raw_plt_texture(const std::string& name)
{
    if (name.empty()) {
        return {};
    }

    const auto cache_key = raw_plt_cache_key(name);
    if (auto it = texture_cache_.find(cache_key); it != texture_cache_.end()) {
        return it->second;
    }

    auto plt_data = nw::kernel::resman().demand({nw::Resref{name}, nw::ResourceType::plt});
    nw::Plt plt{std::move(plt_data)};
    if (!plt.valid()) {
        return {};
    }

    nw::gfx::TextureDesc texture_desc{};
    texture_desc.width = plt.width();
    texture_desc.height = plt.height();
    texture_desc.mip_levels = 1;
    texture_desc.format = nw::gfx::Fmt::RGBA8;
    auto texture = nw::gfx::create_texture(ctx_, texture_desc);
    if (!texture.valid()) {
        return {};
    }

    const size_t pixel_count = static_cast<size_t>(plt.width()) * plt.height();
    std::vector<uint8_t> packed(pixel_count * 4, 0);
    const auto* src = plt.pixels();
    for (size_t i = 0; i < pixel_count; ++i) {
        packed[i * 4 + 0] = src[i].color;
        packed[i * 4 + 1] = static_cast<uint8_t>(src[i].layer);
        packed[i * 4 + 2] = 0;
        packed[i * 4 + 3] = src[i].color == 255 ? 0 : 255;
    }

    nw::gfx::upload_texture_rgba8(ctx_, texture, packed.data(), packed.size());
    texture_cache_[cache_key] = texture;
    return texture;
}

const nw::Image* RenderAssetCache::get_or_load_source_image(const std::string& name)
{
    if (name.empty()) {
        return nullptr;
    }

    if (auto it = image_cache_.find(name); it != image_cache_.end()) {
        return it->second && it->second->valid() ? it->second.get() : nullptr;
    }

    auto image = std::unique_ptr<nw::Image>{nw::kernel::resman().texture(nw::Resref{name})};
    const auto* result = image && image->valid() ? image.get() : nullptr;
    image_cache_[name] = std::move(image);
    return result;
}

ModelInstance* RenderAssetCache::get_or_load_particle_mesh(const std::string& resref)
{
    if (resref.empty()) {
        return nullptr;
    }

    if (auto it = particle_mesh_cache_.find(resref); it != particle_mesh_cache_.end()) {
        return it->second.get();
    }

    ModelLoader loader(ctx_);
    auto model = loader.load(resref);
    auto* result = model.get();
    particle_mesh_cache_.emplace(resref, std::move(model));
    return result;
}

} // namespace nw::render::nwn
