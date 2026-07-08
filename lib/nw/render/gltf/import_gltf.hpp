#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/render/model.hpp>
#include <nw/render/model_asset.hpp>

#include <filesystem>
#include <memory>

namespace nw::render::gltf {

struct ImportGltfDesc {
    nw::gfx::Context* ctx = nullptr;
    nw::gfx::BindlessTextureIndex fallback_albedo = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex fallback_normal = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex fallback_surface = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex fallback_emissive = nw::gfx::kInvalidBindlessTextureIndex;
};

struct GltfImportStats {
    uint32_t source_node_count = 0;
    uint32_t imported_node_count = 0;
    uint32_t material_count = 0;
    uint32_t primitive_count = 0;
    uint32_t texture_source_count = 0;
    uint32_t skin_count = 0;
    uint32_t skeleton_count = 0;
    uint32_t animation_count = 0;
    uint32_t skipped_duplicate_node_count = 0;
    uint32_t skipped_invalid_node_count = 0;
    uint32_t skipped_depth_node_count = 0;
    uint32_t skipped_skin_joint_count = 0;
    uint32_t dropped_primitive_count = 0;
    uint32_t nonfinite_node_transform_count = 0;
    uint32_t nonfinite_position_count = 0;
    uint32_t nonfinite_normal_count = 0;
    uint32_t nonfinite_texcoord_count = 0;
    uint32_t nonfinite_tangent_count = 0;
    uint32_t nonfinite_skin_weight_count = 0;
    uint32_t nonfinite_inverse_bind_matrix_count = 0;
    uint32_t nonfinite_animation_channel_count = 0;

    [[nodiscard]] bool complete() const noexcept
    {
        return skipped_duplicate_node_count == 0
            && skipped_invalid_node_count == 0
            && skipped_depth_node_count == 0
            && skipped_skin_joint_count == 0
            && dropped_primitive_count == 0
            && nonfinite_node_transform_count == 0
            && nonfinite_position_count == 0
            && nonfinite_normal_count == 0
            && nonfinite_texcoord_count == 0
            && nonfinite_tangent_count == 0
            && nonfinite_skin_weight_count == 0
            && nonfinite_inverse_bind_matrix_count == 0
            && nonfinite_animation_channel_count == 0;
    }
};

struct GltfModelAssetImportResult {
    std::unique_ptr<nw::render::ModelAsset> asset;
    GltfImportStats stats;
};

struct GltfRenderModelImportResult {
    std::unique_ptr<nw::render::RenderModel> model;
    bool asset_imported = false;
    GltfImportStats import_stats;
    nw::render::ModelAssetUploadStats geometry_upload_stats;
    nw::render::ModelAssetTextureUploadStats texture_upload_stats;
};

std::unique_ptr<nw::render::RenderModel> import_gltf(const std::filesystem::path& path, const ImportGltfDesc& desc);
std::unique_ptr<nw::render::ModelAsset> import_gltf_model_asset(const std::filesystem::path& path);
GltfModelAssetImportResult import_gltf_model_asset_with_stats(const std::filesystem::path& path);
GltfRenderModelImportResult import_gltf_render_model_from_asset(
    const std::filesystem::path& path,
    const ImportGltfDesc& desc);

} // namespace nw::render::gltf
