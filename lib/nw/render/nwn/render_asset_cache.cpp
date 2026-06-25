#include "render_asset_cache.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/render/texture_decode.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/error_context.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
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

size_t saturating_add(size_t lhs, size_t rhs) noexcept
{
    const size_t max = std::numeric_limits<size_t>::max();
    return max - lhs < rhs ? max : lhs + rhs;
}

size_t saturating_multiply(size_t lhs, size_t rhs) noexcept
{
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    const size_t max = std::numeric_limits<size_t>::max();
    return lhs > max / rhs ? max : lhs * rhs;
}

size_t image_pixel_bytes(const nw::Image* image) noexcept
{
    if (!image || !image->valid()) {
        return 0;
    }
    return saturating_multiply(
        saturating_multiply(static_cast<size_t>(image->width()), image->height()),
        image->channels());
}

size_t particle_mesh_payload_bytes(const ModelInstance* model) noexcept
{
    if (!model) {
        return 0;
    }

    size_t result = 0;
    for (const auto& node : model->nodes_) {
        if (!node || !node->is_mesh) {
            continue;
        }
        const auto* mesh = static_cast<const Mesh*>(node.get());
        const size_t vertex_stride = mesh->is_skin ? sizeof(SkinnedVertex) : sizeof(Vertex);
        result = saturating_add(result, saturating_multiply(mesh->vertex_count, vertex_stride));
        result = saturating_add(result, saturating_multiply(mesh->index_count, sizeof(uint16_t)));
    }
    return result;
}

void copy_rgba_pixels(uint8_t* dst, const uint8_t* src, uint32_t width, uint32_t height,
    bool premultiply, bool flip_rows)
{
    const size_t row_bytes = static_cast<size_t>(width) * 4;
    for (uint32_t y = 0; y < height; ++y) {
        const size_t src_y = flip_rows ? height - 1 - y : y;
        const size_t src_row = static_cast<size_t>(src_y) * row_bytes;
        const size_t dst_row = static_cast<size_t>(y) * row_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            const size_t src_i = src_row + static_cast<size_t>(x) * 4;
            const size_t dst_i = dst_row + static_cast<size_t>(x) * 4;
            const uint8_t a = src[src_i + 3];
            if (premultiply) {
                dst[dst_i + 0] = static_cast<uint8_t>((uint16_t(src[src_i + 0]) * a + 127) / 255);
                dst[dst_i + 1] = static_cast<uint8_t>((uint16_t(src[src_i + 1]) * a + 127) / 255);
                dst[dst_i + 2] = static_cast<uint8_t>((uint16_t(src[src_i + 2]) * a + 127) / 255);
            } else {
                dst[dst_i + 0] = src[src_i + 0];
                dst[dst_i + 1] = src[src_i + 1];
                dst[dst_i + 2] = src[src_i + 2];
            }
            dst[dst_i + 3] = a;
        }
    }
}

void expand_rgb_to_rgba(uint8_t* dst, const uint8_t* src, uint32_t width, uint32_t height, bool flip_rows)
{
    const size_t src_row_bytes = static_cast<size_t>(width) * 3;
    const size_t dst_row_bytes = static_cast<size_t>(width) * 4;
    for (uint32_t y = 0; y < height; ++y) {
        const size_t src_y = flip_rows ? height - 1 - y : y;
        const size_t src_row = static_cast<size_t>(src_y) * src_row_bytes;
        const size_t dst_row = static_cast<size_t>(y) * dst_row_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            const size_t src_i = src_row + static_cast<size_t>(x) * 3;
            const size_t dst_i = dst_row + static_cast<size_t>(x) * 4;
            dst[dst_i + 0] = src[src_i + 0];
            dst[dst_i + 1] = src[src_i + 1];
            dst[dst_i + 2] = src[src_i + 2];
            dst[dst_i + 3] = 255;
        }
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

std::string linear_cache_key(const std::string& name)
{
    return name + "#linear";
}

std::string roughness_surface_cache_key(const std::string& name)
{
    return name + "#roughness_surface";
}

void copy_image_to_rgba(uint8_t* dst, const nw::Image& image, bool flip_rows)
{
    const uint32_t channels = image.channels();
    const size_t dst_row_bytes = static_cast<size_t>(image.width()) * 4;
    const size_t src_row_bytes = static_cast<size_t>(image.width()) * channels;
    for (uint32_t y = 0; y < image.height(); ++y) {
        const size_t src_y = flip_rows ? image.height() - 1 - y : y;
        const size_t src_row = src_y * src_row_bytes;
        const size_t dst_row = static_cast<size_t>(y) * dst_row_bytes;
        for (uint32_t x = 0; x < image.width(); ++x) {
            const size_t src_i = src_row + static_cast<size_t>(x) * channels;
            const size_t dst_i = dst_row + static_cast<size_t>(x) * 4;
            const auto* src = image.data();
            const uint8_t r = channels > 0 ? src[src_i + 0] : 0;
            const uint8_t g = channels > 1 ? src[src_i + 1] : r;
            const uint8_t b = channels > 2 ? src[src_i + 2] : r;
            const uint8_t a = channels > 3 ? src[src_i + 3] : 255;
            dst[dst_i + 0] = r;
            dst[dst_i + 1] = g;
            dst[dst_i + 2] = b;
            dst[dst_i + 3] = a;
        }
    }
}

void pack_roughness_as_surface(uint8_t* dst, const nw::Image& image, bool flip_rows)
{
    const uint32_t channels = image.channels();
    const size_t dst_row_bytes = static_cast<size_t>(image.width()) * 4;
    const size_t src_row_bytes = static_cast<size_t>(image.width()) * channels;
    for (uint32_t y = 0; y < image.height(); ++y) {
        const size_t src_y = flip_rows ? image.height() - 1 - y : y;
        const size_t src_row = src_y * src_row_bytes;
        const size_t dst_row = static_cast<size_t>(y) * dst_row_bytes;
        for (uint32_t x = 0; x < image.width(); ++x) {
            const size_t src_i = src_row + static_cast<size_t>(x) * channels;
            const size_t dst_i = dst_row + static_cast<size_t>(x) * 4;
            const uint8_t roughness = channels > 0 ? image.data()[src_i] : 255;
            dst[dst_i + 0] = 255;
            dst[dst_i + 1] = roughness;
            dst[dst_i + 2] = 0;
            dst[dst_i + 3] = 255;
        }
    }
}

nw::gfx::Handle<nw::gfx::Texture> create_uploaded_rgba_texture(
    nw::gfx::Context* ctx, uint32_t width, uint32_t height, nw::gfx::Fmt format, const std::vector<uint8_t>& pixels)
{
    nw::gfx::TextureDesc texture_desc{};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.mip_levels = full_mip_count(width, height);
    texture_desc.format = format;
    auto texture = nw::gfx::create_texture(ctx, texture_desc);
    if (!texture.valid()) {
        return {};
    }
    nw::gfx::upload_texture_rgba8(ctx, texture, pixels.data(), pixels.size());
    return texture;
}

void log_warning_error_context()
{
    if (nw::error_context_stack) {
        LOG_F(WARNING, "\n{}", nw::get_error_context());
    }
}

nw::ResourceData demand_texture_image_data(const std::string& name)
{
    ERRARE("[render] loading texture '{}' (search order: dds, tga)", std::string_view{name});
    return nw::kernel::resman().demand_in_order(nw::Resref{name}, {nw::ResourceType::dds, nw::ResourceType::tga});
}

} // namespace

void RenderAssetCache::clear(nw::gfx::Handle<nw::gfx::Texture> fallback_texture)
{
    for (auto& [name, cached] : texture_cache_) {
        if (cached.texture.valid() && cached.texture != fallback_texture) {
            nw::gfx::destroy_texture(ctx_, cached.texture);
        }
    }
    texture_cache_.clear();
    image_cache_.clear();
    particle_mesh_cache_.clear();
    clear_model_loader_resource_caches();
}

void RenderAssetCache::destroy(nw::gfx::Handle<nw::gfx::Texture> fallback_texture)
{
    clear(fallback_texture);
}

RenderAssetCacheStats RenderAssetCache::stats() const noexcept
{
    RenderAssetCacheStats result{
        .texture_count = texture_cache_.size(),
        .source_image_count = image_cache_.size(),
        .particle_mesh_count = particle_mesh_cache_.size(),
    };
    for (const auto& [name, cached] : texture_cache_) {
        result.texture_upload_bytes = saturating_add(result.texture_upload_bytes, cached.upload_bytes);
    }
    for (const auto& [name, image] : image_cache_) {
        result.source_image_pixel_bytes = saturating_add(result.source_image_pixel_bytes, image_pixel_bytes(image.get()));
    }
    for (const auto& [name, model] : particle_mesh_cache_) {
        result.particle_mesh_payload_bytes = saturating_add(result.particle_mesh_payload_bytes, particle_mesh_payload_bytes(model.get()));
    }
    return result;
}

bool RenderAssetCache::texture_rows_flipped_on_upload(const std::string& name, bool premultiply_alpha) const
{
    const auto it = texture_cache_.find(alpha_cache_key(name, premultiply_alpha));
    return it != texture_cache_.end() && it->second.rows_flipped;
}

nw::gfx::Handle<nw::gfx::Texture> RenderAssetCache::get_or_load_texture(
    const std::string& name, bool premultiply_alpha, nw::gfx::Handle<nw::gfx::Texture> fallback_texture)
{
    if (name.empty()) {
        return fallback_texture;
    }

    const auto cache_key = alpha_cache_key(name, premultiply_alpha);
    if (auto it = texture_cache_.find(cache_key); it != texture_cache_.end()) {
        return it->second.texture;
    }

    ERRARE("[render] loading texture '{}' (premultiply_alpha={})", std::string_view{name}, premultiply_alpha);
    auto data = demand_texture_image_data(name);
    if (data.bytes.size() == 0) {
        LOG_F(WARNING, "Texture not found: {} (searched: {}.dds, {}.tga)", name, name, name);
        log_warning_error_context();
        texture_cache_[cache_key] = CachedTexture{.texture = fallback_texture};
        return fallback_texture;
    }

    const bool restore_tga_file_rows = nw::render::tga_texture_rows_need_file_order_restore(data);
    auto image = std::make_unique<nw::Image>(std::move(data), true);
    if (!image->valid()) {
        LOG_F(WARNING, "Texture failed to load: {} (using fallback)", name);
        log_warning_error_context();
        texture_cache_[cache_key] = CachedTexture{.texture = fallback_texture};
        return fallback_texture;
    }

    nw::gfx::TextureDesc texture_desc{};
    texture_desc.width = image->width();
    texture_desc.height = image->height();
    texture_desc.mip_levels = full_mip_count(image->width(), image->height());
    texture_desc.format = nw::gfx::Fmt::RGBA8Srgb;
    auto texture = nw::gfx::create_texture(ctx_, texture_desc);
    if (!texture.valid()) {
        texture_cache_[cache_key] = CachedTexture{.texture = fallback_texture};
        return fallback_texture;
    }

    const size_t pixel_count = static_cast<size_t>(image->width()) * image->height();
    size_t upload_bytes = 0;
    if (image->channels() == 4) {
        std::vector<uint8_t> rgba(pixel_count * 4);
        copy_rgba_pixels(rgba.data(), image->data(), image->width(), image->height(), premultiply_alpha, restore_tga_file_rows);
        nw::gfx::upload_texture_rgba8(ctx_, texture, rgba.data(), rgba.size());
        upload_bytes = rgba.size();
    } else if (image->channels() == 3) {
        std::vector<uint8_t> rgba(pixel_count * 4);
        expand_rgb_to_rgba(rgba.data(), image->data(), image->width(), image->height(), restore_tga_file_rows);
        nw::gfx::upload_texture_rgba8(ctx_, texture, rgba.data(), rgba.size());
        upload_bytes = rgba.size();
    } else {
        LOG_F(WARNING, "Texture {} has unsupported {} channels", name, image->channels());
        log_warning_error_context();
        nw::gfx::destroy_texture(ctx_, texture);
        texture_cache_[cache_key] = CachedTexture{.texture = fallback_texture};
        return fallback_texture;
    }

    texture_cache_[cache_key] = CachedTexture{
        .texture = texture,
        .upload_bytes = upload_bytes,
        .rows_flipped = restore_tga_file_rows,
    };
    return texture;
}

nw::gfx::Handle<nw::gfx::Texture> RenderAssetCache::get_or_load_linear_texture(const std::string& name)
{
    if (name.empty()) {
        return {};
    }

    const auto cache_key = linear_cache_key(name);
    if (auto it = texture_cache_.find(cache_key); it != texture_cache_.end()) {
        return it->second.texture;
    }

    ERRARE("[render] loading linear texture '{}'", std::string_view{name});
    auto data = demand_texture_image_data(name);
    if (data.bytes.size() == 0) {
        LOG_F(WARNING, "Texture not found: {} (searched: {}.dds, {}.tga)", name, name, name);
        log_warning_error_context();
        texture_cache_[cache_key] = {};
        return {};
    }

    const bool restore_tga_file_rows = nw::render::tga_texture_rows_need_file_order_restore(data);
    auto image = std::make_unique<nw::Image>(std::move(data), true);
    if (!image->valid() || image->channels() == 0 || !image->data()) {
        LOG_F(WARNING, "Texture failed to load: {} (using no material map)", name);
        log_warning_error_context();
        texture_cache_[cache_key] = {};
        return {};
    }

    const size_t pixel_count = static_cast<size_t>(image->width()) * image->height();
    std::vector<uint8_t> rgba(pixel_count * 4);
    copy_image_to_rgba(rgba.data(), *image, restore_tga_file_rows);

    auto texture = create_uploaded_rgba_texture(ctx_, image->width(), image->height(), nw::gfx::Fmt::RGBA8, rgba);
    if (texture.valid()) {
        texture_cache_[cache_key] = CachedTexture{.texture = texture, .upload_bytes = rgba.size()};
    } else {
        texture_cache_[cache_key] = {};
    }
    return texture;
}

nw::gfx::Handle<nw::gfx::Texture> RenderAssetCache::get_or_load_roughness_surface_texture(const std::string& name)
{
    if (name.empty()) {
        return {};
    }

    const auto cache_key = roughness_surface_cache_key(name);
    if (auto it = texture_cache_.find(cache_key); it != texture_cache_.end()) {
        return it->second.texture;
    }

    ERRARE("[render] loading roughness surface texture '{}'", std::string_view{name});
    auto data = demand_texture_image_data(name);
    if (data.bytes.size() == 0) {
        LOG_F(WARNING, "Texture not found: {} (searched: {}.dds, {}.tga)", name, name, name);
        log_warning_error_context();
        texture_cache_[cache_key] = {};
        return {};
    }

    const bool restore_tga_file_rows = nw::render::tga_texture_rows_need_file_order_restore(data);
    auto image = std::make_unique<nw::Image>(std::move(data), true);
    if (!image->valid() || image->channels() == 0 || !image->data()) {
        LOG_F(WARNING, "Texture failed to load: {} (using no roughness map)", name);
        log_warning_error_context();
        texture_cache_[cache_key] = {};
        return {};
    }

    const size_t pixel_count = static_cast<size_t>(image->width()) * image->height();
    std::vector<uint8_t> surface(pixel_count * 4);
    pack_roughness_as_surface(surface.data(), *image, restore_tga_file_rows);

    auto texture = create_uploaded_rgba_texture(ctx_, image->width(), image->height(), nw::gfx::Fmt::RGBA8, surface);
    if (texture.valid()) {
        texture_cache_[cache_key] = CachedTexture{.texture = texture, .upload_bytes = surface.size()};
    } else {
        texture_cache_[cache_key] = {};
    }
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
        return it->second.texture;
    }

    ERRARE("[render] loading PLT texture '{}'", std::string_view{name});
    auto plt_data = nw::kernel::resman().demand({nw::Resref{name}, nw::ResourceType::plt});
    nw::Plt plt{std::move(plt_data)};
    if (!plt.valid()) {
        return get_or_load_texture(name, premultiply_alpha, fallback_texture);
    }

    nw::Image image{plt, colors};
    if (!image.valid()) {
        texture_cache_[cache_key] = CachedTexture{.texture = fallback_texture};
        return fallback_texture;
    }

    nw::gfx::TextureDesc texture_desc{};
    texture_desc.width = image.width();
    texture_desc.height = image.height();
    texture_desc.mip_levels = full_mip_count(image.width(), image.height());
    texture_desc.format = nw::gfx::Fmt::RGBA8Srgb;
    auto texture = nw::gfx::create_texture(ctx_, texture_desc);
    if (!texture.valid()) {
        texture_cache_[cache_key] = CachedTexture{.texture = fallback_texture};
        return fallback_texture;
    }

    const size_t pixel_count = static_cast<size_t>(image.width()) * image.height();
    std::vector<uint8_t> rgba(pixel_count * 4);
    copy_rgba_pixels(rgba.data(), image.data(), image.width(), image.height(), premultiply_alpha, false);
    nw::gfx::upload_texture_rgba8(ctx_, texture, rgba.data(), rgba.size());
    texture_cache_[cache_key] = CachedTexture{.texture = texture, .upload_bytes = rgba.size()};
    return texture;
}

nw::gfx::Handle<nw::gfx::Texture> RenderAssetCache::get_or_load_raw_plt_texture(const std::string& name)
{
    if (name.empty()) {
        return {};
    }

    const auto cache_key = raw_plt_cache_key(name);
    if (auto it = texture_cache_.find(cache_key); it != texture_cache_.end()) {
        return it->second.texture;
    }

    ERRARE("[render] loading raw PLT texture '{}'", std::string_view{name});
    auto plt_data = nw::kernel::resman().demand({nw::Resref{name}, nw::ResourceType::plt});
    nw::Plt plt{std::move(plt_data)};
    if (!plt.valid()) {
        log_warning_error_context();
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
    texture_cache_[cache_key] = CachedTexture{.texture = texture, .upload_bytes = packed.size()};
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

    auto data = demand_texture_image_data(name);
    if (data.bytes.size() == 0) {
        image_cache_[name] = nullptr;
        return nullptr;
    }

    auto image = std::make_unique<nw::Image>(std::move(data), true);
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
