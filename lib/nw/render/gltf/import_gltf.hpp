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

struct GltfRenderModelImportResult {
    std::unique_ptr<nw::render::RenderModel> model;
    bool asset_imported = false;
    nw::render::ModelAssetUploadStats geometry_upload_stats;
    nw::render::ModelAssetTextureUploadStats texture_upload_stats;
};

std::unique_ptr<nw::render::RenderModel> import_gltf(const std::filesystem::path& path, const ImportGltfDesc& desc);
std::unique_ptr<nw::render::ModelAsset> import_gltf_model_asset(const std::filesystem::path& path);
GltfRenderModelImportResult import_gltf_render_model_from_asset(
    const std::filesystem::path& path,
    const ImportGltfDesc& desc);

} // namespace nw::render::gltf
