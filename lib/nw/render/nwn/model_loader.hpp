#pragma once

#include <nw/model/Mdl.hpp>
#include <nw/render/model.hpp>
#include <nw/render/model_asset.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace nw::render::nwn {

enum class NwnMaterialAlphaProfile {
    opaque,
    binary,
    mostly_binary,
    graded,
};

struct NwnMaterialClassificationInput {
    const nw::model::TrimeshNode* node = nullptr;
    std::string_view bitmap_name;
    nw::model::ModelClass model_class = nw::model::ModelClass::invalid;
    NwnMaterialAlphaProfile alpha_profile = NwnMaterialAlphaProfile::opaque;
    bool is_planar_quad = false;
    bool has_txi = false;
    std::string_view txi_blending;
    bool txi_decal = false;
    bool txi_has_alphamean = false;
    float txi_alphamean = 0.5f;
};

struct ModelLoaderResourceCacheStats {
    size_t mtr_material_count = 0;
    size_t texture_analysis_count = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return mtr_material_count == 0 && texture_analysis_count == 0;
    }
};

struct NwnModelAssetImportStats {
    uint32_t source_node_count = 0;
    uint32_t material_count = 0;
    uint32_t primitive_count = 0;
    uint32_t texture_source_count = 0;
    uint32_t socket_count = 0;
    uint32_t light_count = 0;
    uint32_t deformer_count = 0;
    uint32_t secondary_motion_deformer_count = 0;
    uint32_t unsupported_deformer_count = 0;
    uint32_t particle_system_count = 0;
    uint32_t particle_event_count = 0;
    uint32_t particle_import_warning_count = 0;
    uint32_t skipped_empty_mesh_count = 0;
    uint32_t skipped_skin_mesh_count = 0;
    uint32_t unsupported_specular_texture_count = 0;
    uint32_t unsupported_plt_texture_count = 0;
    uint32_t missing_texture_source_count = 0;
    uint32_t normal_repair_count = 0;
    uint32_t tangent_repair_count = 0;
    uint32_t water_name_heuristic_count = 0;
    uint32_t foliage_name_heuristic_count = 0;
    uint32_t texture_source_overflow_count = 0;
    uint32_t deformer_overflow_count = 0;
    uint32_t primitive_overflow_count = 0;
    uint32_t particle_system_overflow_count = 0;

    [[nodiscard]] bool complete() const noexcept
    {
        return skipped_empty_mesh_count == 0
            && skipped_skin_mesh_count == 0
            && unsupported_specular_texture_count == 0
            && unsupported_plt_texture_count == 0
            && missing_texture_source_count == 0
            && texture_source_overflow_count == 0
            && deformer_overflow_count == 0
            && primitive_overflow_count == 0
            && particle_system_overflow_count == 0;
    }
};

struct NwnModelAssetImportResult {
    std::unique_ptr<nw::render::ModelAsset> asset;
    NwnModelAssetImportStats stats;
};

struct NwnRenderModelImportResult {
    std::unique_ptr<nw::render::RenderModel> model;
    NwnModelAssetImportStats import_stats;
    nw::render::ModelAssetUploadStats geometry_upload_stats;
    nw::render::ModelAssetTextureUploadStats texture_upload_stats;
};

enum class DanglyDeformPolicy {
    secondary_motion_chain,
    foliage_sway,
};

[[nodiscard]] DanglyDeformPolicy dangly_deform_policy_for(const nw::model::DanglymeshNode* node);
[[nodiscard]] std::string_view dangly_deform_policy_name(DanglyDeformPolicy policy) noexcept;
[[nodiscard]] nw::render::ModelDeformerKind model_deformer_kind_for(DanglyDeformPolicy policy) noexcept;
[[nodiscard]] std::string nwn_humanoid_palette_resref(std::string_view model_resref);
[[nodiscard]] std::string resolve_nwn_model_albedo_resref(
    std::string_view model_resref, std::string_view albedo_resref);
[[nodiscard]] MaterialMode classify_nwn_material(const NwnMaterialClassificationInput& input);
[[nodiscard]] NwnModelAssetImportResult import_nwn_model_asset(const nw::model::Mdl& mdl);
[[nodiscard]] NwnRenderModelImportResult import_nwn_render_model(
    const nw::model::Mdl& mdl, const nw::render::ModelAssetTextureUploadDesc& texture_upload);
void clear_model_loader_resource_caches();
[[nodiscard]] ModelLoaderResourceCacheStats model_loader_resource_cache_stats();

} // namespace nw::render::nwn
