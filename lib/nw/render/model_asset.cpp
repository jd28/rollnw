#include "model_asset.hpp"
#include "texture_decode.hpp"

#include <nw/formats/Image.hpp>
#include <nw/formats/Plt.hpp>
#include <nw/util/string.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace nw::render {

uint32_t RenderModel::socket_index(std::string_view socket_name) const noexcept
{
    if (socket_name.empty()) {
        return kInvalidModelNodeIndex;
    }

    for (size_t i = 0; i < sockets.size() && i < kInvalidModelNodeIndex; ++i) {
        if (nw::string::icmp(sockets[i].name, socket_name)) {
            return static_cast<uint32_t>(i);
        }
    }
    return kInvalidModelNodeIndex;
}

const ModelSocket* RenderModel::socket(uint32_t index) const noexcept
{
    return index < sockets.size() ? &sockets[index] : nullptr;
}

const Node* RenderModel::socket_node(uint32_t index) const noexcept
{
    const auto* record = socket(index);
    if (!record || record->source_node_index >= nodes.size()) {
        return nullptr;
    }
    return &nodes[record->source_node_index];
}

namespace {

uint32_t saturating_asset_count(size_t count) noexcept
{
    return count > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(count);
}

bool primitive_indices_in_range(const ModelAssetPrimitive& primitive) noexcept
{
    const size_t vertex_count = primitive.vertex_count();
    for (const uint32_t index : primitive.indices) {
        if (index >= vertex_count) {
            return false;
        }
    }
    return true;
}

bool primitive_skin_supported(const ModelAsset& asset, const ModelAssetPrimitive& primitive) noexcept
{
    if (!primitive.uses_skinned_vertices()) {
        return true;
    }
    if (primitive.skin >= asset.skins.size()) {
        return false;
    }
    return model_skin_bone_count_supported(asset.skins[primitive.skin].joints.size());
}

bool node_parent_valid(const Node& node, size_t node_index, size_t node_count) noexcept
{
    return node.parent < 0
        || (static_cast<size_t>(node.parent) < node_count
            && static_cast<size_t>(node.parent) != node_index);
}

bool source_node_index_valid(uint32_t index, size_t node_count) noexcept
{
    return index != kInvalidModelNodeIndex && index < node_count;
}

bool skeleton_eval_order_valid(const Skeleton& skeleton) noexcept
{
    if (skeleton.joints.empty() || skeleton.eval_order.size() != skeleton.joints.size()) {
        return false;
    }

    std::vector<uint8_t> seen(skeleton.joints.size(), 0u);
    for (const uint32_t index : skeleton.eval_order) {
        if (index >= skeleton.joints.size()) {
            return false;
        }
        auto& row = seen[index];
        if (row != 0u) {
            return false;
        }
        row = 1u;
    }
    return true;
}

bool skeleton_node_to_joint_valid(const Skeleton& skeleton) noexcept
{
    size_t expected_count = 0;
    for (const auto& joint : skeleton.joints) {
        if (joint.node >= 0) {
            expected_count = std::max(expected_count, static_cast<size_t>(joint.node) + 1u);
        }
    }
    if (skeleton.node_to_joint.size() != expected_count) {
        return false;
    }
    for (size_t joint_index = 0; joint_index < skeleton.joints.size(); ++joint_index) {
        const int32_t node = skeleton.joints[joint_index].node;
        if (node < 0 || static_cast<size_t>(node) >= skeleton.node_to_joint.size()) {
            continue;
        }
        if (skeleton.node_to_joint[static_cast<size_t>(node)] != static_cast<int32_t>(joint_index)) {
            return false;
        }
    }
    return true;
}

bool valid_texture_source_index(uint32_t index, size_t texture_source_count) noexcept
{
    return index == kInvalidModelAssetTextureSourceIndex || index < texture_source_count;
}

bool material_texture_sources_valid(const ModelAsset& asset, const ModelAssetMaterialTextureSources& sources) noexcept
{
    const size_t texture_source_count = asset.texture_sources.size();
    return valid_texture_source_index(sources.albedo, texture_source_count)
        && valid_texture_source_index(sources.normal, texture_source_count)
        && valid_texture_source_index(sources.metallic_roughness, texture_source_count)
        && valid_texture_source_index(sources.occlusion, texture_source_count)
        && valid_texture_source_index(sources.emissive, texture_source_count);
}

bool source_index_valid(uint32_t index) noexcept
{
    return index != kInvalidModelAssetTextureSourceIndex;
}

bool material_sources_reference_texture(const ModelAssetMaterialTextureSources& sources) noexcept
{
    return source_index_valid(sources.albedo)
        || source_index_valid(sources.normal)
        || source_index_valid(sources.metallic_roughness)
        || source_index_valid(sources.occlusion)
        || source_index_valid(sources.emissive);
}

bool model_asset_texture_source_is_plt(const ModelAssetTextureSource& source) noexcept
{
    if (source.resource.valid()) {
        return source.resource.type == nw::ResourceType::plt;
    }
    if (source.path.empty()) {
        return false;
    }
    const auto resource = nw::Resource::from_filename(source.path);
    return resource.valid() && resource.type == nw::ResourceType::plt;
}

bool material_mode_casts_shadow(MaterialMode mode) noexcept
{
    switch (mode) {
    case MaterialMode::opaque:
    case MaterialMode::cutout:
        return true;
    case MaterialMode::water:
    case MaterialMode::transparent:
        return false;
    }
    return false;
}

void count_missing_context(ModelAssetTextureUploadStats& stats) noexcept
{
    if (stats.missing_context_count == 0) {
        stats.missing_context_count = 1;
    }
}

bool encoded_texture_source_is_dds(const std::vector<uint8_t>& bytes) noexcept
{
    return bytes.size() >= 4
        && bytes[0] == 'D'
        && bytes[1] == 'D'
        && bytes[2] == 'S'
        && bytes[3] == ' ';
}

nw::Resource encoded_texture_source_resource(const ModelAssetTextureSource& source) noexcept
{
    if (source.resource.valid()) {
        return source.resource;
    }
    if (encoded_texture_source_is_dds(source.encoded_bytes)) {
        return nw::Resource{nw::Resref{"encoded"}, nw::ResourceType::dds};
    }
    return {};
}

uint32_t full_mip_count(uint32_t width, uint32_t height) noexcept
{
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(width / 2u, 1u);
        height = std::max(height / 2u, 1u);
        ++levels;
    }
    return levels;
}

ModelAssetDecodedTexture decode_model_asset_plt_source_rgba8(nw::ResourceData data,
    ModelAssetTextureUploadStats& stats)
{
    nw::Plt plt{std::move(data)};
    if (!plt.valid() || plt.width() == 0 || plt.height() == 0 || !plt.pixels()) {
        ++stats.decode_failure_count;
        return {};
    }

    ModelAssetDecodedTexture result{};
    result.width = static_cast<int>(plt.width());
    result.height = static_cast<int>(plt.height());
    result.pixels.resize(static_cast<size_t>(plt.width()) * plt.height() * 4);
    const auto* src = plt.pixels();
    const size_t pixel_count = static_cast<size_t>(plt.width()) * plt.height();
    for (size_t i = 0; i < pixel_count; ++i) {
        result.pixels[i * 4 + 0] = src[i].color;
        result.pixels[i * 4 + 1] = static_cast<uint8_t>(src[i].layer);
        result.pixels[i * 4 + 2] = 0;
        result.pixels[i * 4 + 3] = src[i].color == 255 ? 0 : 255;
    }
    return result;
}

} // namespace

ModelAssetDecodedTexture decode_model_asset_texture_source_rgba8(
    const ModelAssetTextureSource& source, ModelAssetTextureUploadStats& stats)
{
    nw::Image image;
    bool restore_tga_file_rows = false;
    switch (source.kind) {
    case ModelAssetTextureSourceKind::external_file:
        if (source.path.empty()) {
            ++stats.missing_source_payload_count;
            return {};
        }
        {
            auto data = nw::ResourceData::from_file(source.path);
            if (data.name.type == nw::ResourceType::plt) {
                return decode_model_asset_plt_source_rgba8(std::move(data), stats);
            }
            restore_tga_file_rows = tga_texture_rows_need_file_order_restore(data);
            image = nw::Image{std::move(data), false};
        }
        break;
    case ModelAssetTextureSourceKind::encoded_bytes:
        if (source.encoded_bytes.empty()) {
            ++stats.missing_source_payload_count;
            return {};
        }
        {
            nw::ResourceData data;
            data.name = encoded_texture_source_resource(source);
            data.bytes = nw::ByteArray{source.encoded_bytes.data(), source.encoded_bytes.size()};
            if (data.name.type == nw::ResourceType::plt) {
                return decode_model_asset_plt_source_rgba8(std::move(data), stats);
            }
            restore_tga_file_rows = tga_texture_rows_need_file_order_restore(data);
            image = nw::Image{std::move(data), false};
        }
        break;
    }

    if (!image.valid() || image.width() == 0 || image.height() == 0 || !image.data()) {
        ++stats.decode_failure_count;
        return {};
    }

    const uint32_t channels = image.channels();
    if (channels == 0 || channels > 4) {
        ++stats.decode_failure_count;
        return {};
    }

    ModelAssetDecodedTexture result{};
    result.width = static_cast<int>(image.width());
    result.height = static_cast<int>(image.height());
    result.pixels.resize(static_cast<size_t>(image.width()) * image.height() * 4);
    const uint8_t* source_pixels = image.data();
    const auto width = image.width();
    const auto height = image.height();
    const size_t src_row_bytes = static_cast<size_t>(width) * channels;
    const size_t dst_row_bytes = static_cast<size_t>(width) * 4;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t src_y = restore_tga_file_rows ? height - 1 - y : y;
        const size_t src_row = static_cast<size_t>(src_y) * src_row_bytes;
        const size_t dst_row = static_cast<size_t>(y) * dst_row_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* src = source_pixels + src_row + static_cast<size_t>(x) * channels;
            uint8_t* dst = result.pixels.data() + dst_row + static_cast<size_t>(x) * 4;
            switch (channels) {
            case 1:
                dst[0] = src[0];
                dst[1] = src[0];
                dst[2] = src[0];
                dst[3] = 255;
                break;
            case 2:
                dst[0] = src[0];
                dst[1] = src[0];
                dst[2] = src[0];
                dst[3] = src[1];
                break;
            case 3:
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
                break;
            default:
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
                break;
            }
        }
    }
    return result;
}

namespace {

nw::gfx::BindlessTextureIndex upload_decoded_texture(nw::gfx::Context* ctx,
    RenderModel& model,
    const ModelAssetDecodedTexture& decoded,
    nw::gfx::Fmt format,
    ModelAssetTextureUploadStats& stats)
{
    if (!ctx) {
        count_missing_context(stats);
        return nw::gfx::kInvalidBindlessTextureIndex;
    }
    if (!decoded.valid()) {
        return nw::gfx::kInvalidBindlessTextureIndex;
    }

    nw::gfx::TextureDesc desc{};
    desc.width = static_cast<uint32_t>(decoded.width);
    desc.height = static_cast<uint32_t>(decoded.height);
    desc.mip_levels = full_mip_count(desc.width, desc.height);
    desc.format = format;
    auto handle = nw::gfx::create_texture(ctx, desc);
    if (!handle.valid()) {
        ++stats.texture_create_failure_count;
        return nw::gfx::kInvalidBindlessTextureIndex;
    }
    if (!nw::gfx::upload_texture_rgba8(ctx, handle, decoded.pixels.data(), decoded.pixels.size())) {
        ++stats.texture_upload_failure_count;
        nw::gfx::destroy_texture(ctx, handle);
        return nw::gfx::kInvalidBindlessTextureIndex;
    }

    const auto texture_index = nw::gfx::get_bindless_texture_index(ctx, handle);
    if (!nw::gfx::bindless_texture_index_valid(texture_index)) {
        ++stats.bindless_failure_count;
        nw::gfx::destroy_texture(ctx, handle);
        return nw::gfx::kInvalidBindlessTextureIndex;
    }

    model.textures.push_back(handle);
    ++stats.uploaded_texture_count;
    return texture_index;
}

nw::gfx::BindlessTextureIndex upload_source_texture(uint32_t source_index,
    nw::gfx::Fmt format,
    nw::gfx::BindlessTextureIndex fallback,
    const ModelAsset& asset,
    const ModelAssetTextureUploadDesc& desc,
    RenderModel& model,
    std::vector<nw::gfx::BindlessTextureIndex>& uploaded_indices,
    std::vector<uint8_t>& attempted,
    ModelAssetTextureUploadStats& stats,
    bool& material_uses_fallback)
{
    if (!source_index_valid(source_index)) {
        return fallback;
    }

    if (attempted[source_index]) {
        const auto cached = uploaded_indices[source_index];
        if (nw::gfx::bindless_texture_index_valid(cached)) {
            return cached;
        }
        material_uses_fallback = true;
        return fallback;
    }
    attempted[source_index] = 1;

    if (!desc.ctx) {
        count_missing_context(stats);
        material_uses_fallback = true;
        return fallback;
    }

    const auto decoded = decode_model_asset_texture_source_rgba8(asset.texture_sources[source_index], stats);
    if (!decoded.valid()) {
        material_uses_fallback = true;
        return fallback;
    }

    const auto texture_index = upload_decoded_texture(desc.ctx,
        model,
        decoded,
        model_asset_texture_source_is_plt(asset.texture_sources[source_index]) ? nw::gfx::Fmt::RGBA8 : format,
        stats);
    if (!nw::gfx::bindless_texture_index_valid(texture_index)) {
        material_uses_fallback = true;
        return fallback;
    }

    uploaded_indices[source_index] = texture_index;
    return texture_index;
}

nw::gfx::BindlessTextureIndex upload_surface_texture(const ModelAssetMaterialTextureSources& sources,
    const ModelAsset& asset,
    const ModelAssetTextureUploadDesc& desc,
    RenderModel& model,
    ModelAssetTextureUploadStats& stats,
    bool& material_uses_fallback)
{
    const bool has_metallic_roughness = source_index_valid(sources.metallic_roughness);
    const bool has_occlusion = source_index_valid(sources.occlusion);
    if (!has_metallic_roughness && !has_occlusion) {
        return desc.fallback_surface;
    }

    if (!desc.ctx) {
        count_missing_context(stats);
        material_uses_fallback = true;
        return desc.fallback_surface;
    }

    ModelAssetDecodedTexture metallic_roughness{};
    ModelAssetDecodedTexture occlusion{};
    bool source_payload_failed = false;
    if (has_metallic_roughness) {
        metallic_roughness = decode_model_asset_texture_source_rgba8(asset.texture_sources[sources.metallic_roughness], stats);
        source_payload_failed = source_payload_failed || !metallic_roughness.valid();
    }
    if (has_occlusion) {
        occlusion = decode_model_asset_texture_source_rgba8(asset.texture_sources[sources.occlusion], stats);
        source_payload_failed = source_payload_failed || !occlusion.valid();
    }

    const int width = metallic_roughness.valid() ? metallic_roughness.width : occlusion.width;
    const int height = metallic_roughness.valid() ? metallic_roughness.height : occlusion.height;
    if (width <= 0 || height <= 0) {
        material_uses_fallback = true;
        return desc.fallback_surface;
    }
    if ((metallic_roughness.valid() && (metallic_roughness.width != width || metallic_roughness.height != height))
        || (occlusion.valid() && (occlusion.width != width || occlusion.height != height))) {
        ++stats.surface_size_mismatch_count;
        material_uses_fallback = true;
        return desc.fallback_surface;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, kModelSurfaceNeutralAlpha);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        pixels[i * 4 + 0] = occlusion.valid() ? occlusion.pixels[i * 4 + 0] : kModelSurfaceNeutralOcclusion;
        pixels[i * 4 + 1] = metallic_roughness.valid() ? metallic_roughness.pixels[i * 4 + 1] : kModelSurfaceNeutralRoughness;
        pixels[i * 4 + 2] = metallic_roughness.valid() ? metallic_roughness.pixels[i * 4 + 2] : kModelSurfaceNeutralMetallic;
        pixels[i * 4 + 3] = kModelSurfaceNeutralAlpha;
    }

    ModelAssetDecodedTexture surface{};
    surface.width = width;
    surface.height = height;
    surface.pixels = std::move(pixels);
    const auto texture_index = upload_decoded_texture(desc.ctx, model, surface, nw::gfx::Fmt::RGBA8, stats);
    if (!nw::gfx::bindless_texture_index_valid(texture_index)) {
        material_uses_fallback = true;
        return desc.fallback_surface;
    }

    material_uses_fallback = material_uses_fallback || source_payload_failed;
    return texture_index;
}

bool create_buffer_from_bytes(nw::gfx::Context* ctx, const void* data, size_t size, nw::gfx::BufferUsage usage,
    nw::gfx::Handle<nw::gfx::Buffer>& out)
{
    if (!ctx || !data || size == 0) return false;

    nw::gfx::BufferDesc desc{};
    desc.size = size;
    desc.usage = usage;
    desc.cpu_visible = true;
    out = nw::gfx::create_buffer(ctx, desc);
    if (!out.valid()) return false;

    void* mapped = nw::gfx::map_buffer(out);
    if (!mapped) {
        nw::gfx::destroy_buffer(out);
        out = {};
        return false;
    }
    std::memcpy(mapped, data, size);
    nw::gfx::unmap_buffer(out);
    return true;
}

void destroy_primitive_buffers(Primitive& primitive) noexcept
{
    if (primitive.vertices.valid()) {
        nw::gfx::destroy_buffer(primitive.vertices);
        primitive.vertices = {};
    }
    if (primitive.indices.valid()) {
        nw::gfx::destroy_buffer(primitive.indices);
        primitive.indices = {};
    }
}

void destroy_render_model_buffers(RenderModel& model) noexcept
{
    for (auto& primitive : model.primitives) {
        destroy_primitive_buffers(primitive);
    }
}

} // namespace

ModelAssetValidationStats validate_model_asset(const ModelAsset& asset) noexcept
{
    ModelAssetValidationStats stats{};
    stats.primitive_count = saturating_asset_count(asset.primitives.size());
    stats.node_count = saturating_asset_count(asset.nodes.size());
    stats.deformer_count = saturating_asset_count(asset.deformers.size());
    stats.skin_count = saturating_asset_count(asset.skins.size());
    stats.skeleton_count = saturating_asset_count(asset.skeletons.size());
    stats.animation_count = saturating_asset_count(asset.animations.size());
    stats.particle_system_count = saturating_asset_count(asset.particle_systems.size());
    stats.texture_source_count = saturating_asset_count(asset.texture_sources.size());
    stats.material_texture_binding_count = saturating_asset_count(asset.material_texture_sources.size());

    for (size_t node_index = 0; node_index < asset.nodes.size(); ++node_index) {
        if (!node_parent_valid(asset.nodes[node_index], node_index, asset.nodes.size())) {
            ++stats.invalid_node_parent_count;
        }
    }

    for (const auto& deformer : asset.deformers) {
        if (!source_node_index_valid(deformer.source_node_index, asset.nodes.size())) {
            ++stats.invalid_deformer_source_node_count;
        }
        if (deformer.vertex_count == 0) {
            ++stats.empty_deformer_vertex_count;
        }
    }

    for (const auto& skin : asset.skins) {
        if (skin.skeleton >= asset.skeletons.size()) {
            ++stats.invalid_skin_skeleton_count;
        }
        if (!model_skin_bone_count_supported(skin.joints.size())) {
            ++stats.unsupported_skin_bone_count;
        }
        if (skin.joints.empty()) {
            ++stats.invalid_skin_joint_count;
        }
        for (const int32_t joint : skin.joints) {
            if (joint < 0 || static_cast<size_t>(joint) >= asset.nodes.size()) {
                ++stats.invalid_skin_joint_count;
                break;
            }
        }
        if (skin.inverse_bind_matrices.size() != skin.joints.size()) {
            ++stats.invalid_skin_inverse_bind_count;
        }
    }

    for (const auto& skeleton : asset.skeletons) {
        for (size_t joint_index = 0; joint_index < skeleton.joints.size(); ++joint_index) {
            const auto& joint = skeleton.joints[joint_index];
            if (joint.parent >= 0
                && (static_cast<size_t>(joint.parent) >= skeleton.joints.size()
                    || static_cast<size_t>(joint.parent) == joint_index)) {
                ++stats.invalid_skeleton_joint_parent_count;
                break;
            }
        }
        for (const auto& joint : skeleton.joints) {
            if (joint.node < 0 || static_cast<size_t>(joint.node) >= asset.nodes.size()) {
                ++stats.invalid_skeleton_joint_node_count;
                break;
            }
        }
        if (!skeleton_eval_order_valid(skeleton)) {
            ++stats.invalid_skeleton_eval_order_count;
        }
        if (!skeleton_node_to_joint_valid(skeleton)) {
            ++stats.invalid_skeleton_node_to_joint_count;
        }
    }

    for (const auto& clip : asset.animations) {
        if (clip.duration < 0.0f) {
            ++stats.invalid_animation_duration_count;
        }
        if (clip.skeleton >= asset.skeletons.size()) {
            ++stats.invalid_animation_skeleton_count;
            continue;
        }
        if (clip.tracks.size() != asset.skeletons[clip.skeleton].joints.size()) {
            ++stats.invalid_animation_track_count;
        }
    }

    if (!asset.material_texture_sources.empty() && asset.material_texture_sources.size() != asset.materials.size()) {
        ++stats.invalid_material_texture_binding_count;
    }
    for (const auto& sources : asset.material_texture_sources) {
        if (!material_texture_sources_valid(asset, sources)) {
            ++stats.invalid_material_texture_binding_count;
        }
    }

    for (const auto& primitive : asset.primitives) {
        bool valid = true;
        if (primitive.vertex_count() == 0) {
            ++stats.empty_vertex_payload_count;
            valid = false;
        }
        if (primitive.indices.empty()) {
            ++stats.empty_index_payload_count;
            valid = false;
        } else if (!primitive_indices_in_range(primitive)) {
            ++stats.index_out_of_range_count;
            valid = false;
        }
        if (primitive.material >= asset.materials.size()) {
            ++stats.invalid_material_count;
            valid = false;
        }
        if (primitive.uses_skinned_vertices()) {
            if (primitive.skin >= asset.skins.size()) {
                ++stats.invalid_skin_count;
                valid = false;
            } else if (!primitive_skin_supported(asset, primitive)) {
                valid = false;
            }
        }
        if (primitive.node >= 0 && static_cast<size_t>(primitive.node) >= asset.nodes.size()) {
            ++stats.invalid_node_count;
            valid = false;
        }
        if (primitive.deformer != kInvalidModelDeformerIndex && primitive.deformer >= asset.deformers.size()) {
            ++stats.invalid_deformer_count;
            valid = false;
        }
        if (valid) {
            ++stats.valid_primitive_count;
        }
    }

    return stats;
}

ModelShadowSummary summarize_model_asset_shadows(const ModelAsset& asset) noexcept
{
    ModelShadowSummary result{};
    result.bounds = asset.bounds;
    result.valid = true;

    for (const auto& primitive : asset.primitives) {
        if (!primitive.casts_shadow || primitive.material >= asset.materials.size()) {
            continue;
        }
        if (!material_mode_casts_shadow(asset.materials[primitive.material].alpha_mode)) {
            continue;
        }
        if (result.caster_count != std::numeric_limits<uint32_t>::max()) {
            ++result.caster_count;
        }
    }
    result.casts_shadow = result.caster_count != 0;
    return result;
}

ModelShadowSummary summarize_render_model_shadows(const RenderModel& model) noexcept
{
    ModelShadowSummary result{};
    result.bounds = model.bounds;
    result.valid = true;

    for (const auto& primitive : model.primitives) {
        if (!primitive.casts_shadow || primitive.material >= model.materials.size()) {
            continue;
        }
        if (!material_mode_casts_shadow(model.materials[primitive.material].alpha_mode)) {
            continue;
        }
        if (result.caster_count != std::numeric_limits<uint32_t>::max()) {
            ++result.caster_count;
        }
    }
    result.casts_shadow = result.caster_count != 0;
    return result;
}

bool upload_model_asset_primitive(nw::gfx::Context* ctx, const ModelAssetPrimitive& source, Primitive& out)
{
    if (!ctx) return false;
    if (source.vertex_count() == 0 || source.indices.empty()) return false;
    if (!primitive_indices_in_range(source)) return false;
    if (source.vertex_count() > std::numeric_limits<uint32_t>::max()
        || source.indices.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    Primitive uploaded{};
    uploaded.vertex_count = static_cast<uint32_t>(source.vertex_count());
    uploaded.index_count = static_cast<uint32_t>(source.indices.size());
    uploaded.material = source.material;
    uploaded.skin = source.skin;
    uploaded.deformer = source.deformer;
    uploaded.node = source.node;
    uploaded.skinned = source.uses_skinned_vertices();
    uploaded.casts_shadow = source.casts_shadow;
    uploaded.transform = source.transform;
    uploaded.inverse_mesh_transform = glm::inverse(source.transform);
    uploaded.bounds = source.bounds;

    std::vector<uint16_t> indices16;
    const void* index_data = nullptr;
    size_t index_bytes = 0;
    if (source.vertex_count() <= std::numeric_limits<uint16_t>::max()) {
        indices16.resize(source.indices.size());
        for (size_t i = 0; i < source.indices.size(); ++i) {
            indices16[i] = static_cast<uint16_t>(source.indices[i]);
        }
        uploaded.index_stride = 2;
        index_data = indices16.data();
        index_bytes = indices16.size() * sizeof(uint16_t);
    } else {
        uploaded.index_stride = 4;
        index_data = source.indices.data();
        index_bytes = source.indices.size() * sizeof(uint32_t);
    }

    bool buffer_ok = false;
    if (source.uses_skinned_vertices()) {
        buffer_ok = create_buffer_from_bytes(ctx,
                        source.skinned_vertices.data(),
                        source.skinned_vertices.size() * sizeof(SkinnedVertex),
                        nw::gfx::BufferUsage::Vertex,
                        uploaded.vertices)
            && create_buffer_from_bytes(ctx, index_data, index_bytes, nw::gfx::BufferUsage::Index, uploaded.indices);
    } else {
        buffer_ok = create_buffer_from_bytes(ctx,
                        source.vertices.data(),
                        source.vertices.size() * sizeof(Vertex),
                        nw::gfx::BufferUsage::Vertex,
                        uploaded.vertices)
            && create_buffer_from_bytes(ctx, index_data, index_bytes, nw::gfx::BufferUsage::Index, uploaded.indices);
    }

    if (!buffer_ok) {
        destroy_primitive_buffers(uploaded);
        return false;
    }

    out = uploaded;
    return true;
}

ModelAssetUploadResult upload_model_asset(const ModelAsset& asset, nw::gfx::Context* ctx)
{
    ModelAssetUploadResult result{};
    result.stats.primitive_count = saturating_asset_count(asset.primitives.size());

    const auto validation = validate_model_asset(asset);
    result.stats.invalid_primitive_count = validation.invalid_primitive_count();
    result.stats.invalid_asset_row_count = validation.invalid_asset_row_count();
    result.stats.invalid_material_texture_binding_count = validation.invalid_material_texture_binding_count;
    if (!validation.passed()) {
        return result;
    }
    if (!ctx) {
        result.stats.missing_context_count = 1;
        return result;
    }
    if (asset.primitives.empty()) {
        return result;
    }

    auto model = std::make_unique<RenderModel>();
    model->materials = asset.materials;
    model->nodes = asset.nodes;
    model->sockets = asset.sockets;
    model->deformers = asset.deformers;
    model->skins = asset.skins;
    model->skeletons = asset.skeletons;
    model->animations = asset.animations;
    model->particle_systems = asset.particle_systems;
    model->shadow = summarize_model_asset_shadows(asset);
    model->bounds = asset.bounds;
    model->source_kind = asset.source_kind;
    model->name = asset.name;
    model->primitives.reserve(asset.primitives.size());

    for (const auto& source : asset.primitives) {
        Primitive primitive{};
        if (!upload_model_asset_primitive(ctx, source, primitive)) {
            ++result.stats.buffer_failure_count;
            destroy_render_model_buffers(*model);
            return result;
        }
        if (primitive.index_stride == 2) {
            ++result.stats.index16_primitive_count;
        } else {
            ++result.stats.index32_primitive_count;
        }
        model->primitives.push_back(primitive);
        ++result.stats.uploaded_primitive_count;
    }

    result.model = std::move(model);
    return result;
}

ModelAssetTextureUploadStats upload_model_asset_material_textures(
    const ModelAsset& asset, const ModelAssetTextureUploadDesc& desc, RenderModel& model)
{
    ModelAssetTextureUploadStats stats{};
    stats.material_count = saturating_asset_count(asset.materials.size());
    stats.texture_source_count = saturating_asset_count(asset.texture_sources.size());
    stats.material_texture_binding_count = saturating_asset_count(asset.material_texture_sources.size());

    model.materials = asset.materials;

    const auto validation = validate_model_asset(asset);
    stats.invalid_material_texture_binding_count = validation.invalid_material_texture_binding_count;
    if (stats.invalid_material_texture_binding_count != 0) {
        return stats;
    }

    std::vector<nw::gfx::BindlessTextureIndex> srgb_indices(
        asset.texture_sources.size(), nw::gfx::kInvalidBindlessTextureIndex);
    std::vector<nw::gfx::BindlessTextureIndex> linear_indices(
        asset.texture_sources.size(), nw::gfx::kInvalidBindlessTextureIndex);
    std::vector<uint8_t> srgb_attempted(asset.texture_sources.size(), 0);
    std::vector<uint8_t> linear_attempted(asset.texture_sources.size(), 0);

    for (size_t i = 0; i < model.materials.size(); ++i) {
        const ModelAssetMaterialTextureSources sources = asset.material_texture_sources.empty()
            ? ModelAssetMaterialTextureSources{}
            : asset.material_texture_sources[i];

        bool material_uses_fallback = false;
        auto& material = model.materials[i];
        material.albedo_uses_plt = source_index_valid(sources.albedo)
            && model_asset_texture_source_is_plt(asset.texture_sources[sources.albedo]);
        material.albedo_index = upload_source_texture(sources.albedo,
            nw::gfx::Fmt::RGBA8Srgb,
            desc.fallback_albedo,
            asset,
            desc,
            model,
            srgb_indices,
            srgb_attempted,
            stats,
            material_uses_fallback);
        material.normal_index = upload_source_texture(sources.normal,
            nw::gfx::Fmt::RGBA8,
            desc.fallback_normal,
            asset,
            desc,
            model,
            linear_indices,
            linear_attempted,
            stats,
            material_uses_fallback);
        material.surface_index = upload_surface_texture(sources, asset, desc, model, stats, material_uses_fallback);
        material.emissive_index = upload_source_texture(sources.emissive,
            nw::gfx::Fmt::RGBA8Srgb,
            desc.fallback_emissive,
            asset,
            desc,
            model,
            srgb_indices,
            srgb_attempted,
            stats,
            material_uses_fallback);

        material.material_uses_fallback = material.material_uses_fallback || material_uses_fallback;
        if (material_uses_fallback && material_sources_reference_texture(sources)) {
            ++stats.fallback_material_count;
        }
    }

    return stats;
}

} // namespace nw::render
