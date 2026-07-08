#pragma once

#include "model.hpp"
#include "particle_def.hpp"

#include <nw/resources/assets.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace nw::render {

inline constexpr uint32_t kInvalidModelAssetTextureSourceIndex = std::numeric_limits<uint32_t>::max();

enum class ModelAssetTextureSourceKind : uint8_t {
    external_file,
    encoded_bytes,
};

struct ModelAssetTextureSource {
    // Compact source texture reference for import/compile/upload boundaries.
    // This stores source bytes or a file path, not decoded image pixels. Runtime
    // assets should drop this table after GPU texture creation unless an
    // editor/debug cache explicitly needs source payloads. Encoded byte sources
    // carry optional source identity so decoders can preserve format-specific
    // behavior such as DDS payload handling without re-querying resource systems.
    ModelAssetTextureSourceKind kind = ModelAssetTextureSourceKind::external_file;
    nw::Resource resource;
    std::string path;
    std::vector<uint8_t> encoded_bytes;
};

struct ModelAssetMaterialTextureSources {
    // PBR source texture bindings by material index. Invalid means the material
    // has no source payload for that role. metallic_roughness and occlusion are
    // kept separate because glTF supplies them separately and the current
    // renderer packs them into the ORM surface texture at upload time.
    uint32_t albedo = kInvalidModelAssetTextureSourceIndex;
    uint32_t normal = kInvalidModelAssetTextureSourceIndex;
    uint32_t metallic_roughness = kInvalidModelAssetTextureSourceIndex;
    uint32_t occlusion = kInvalidModelAssetTextureSourceIndex;
    uint32_t emissive = kInvalidModelAssetTextureSourceIndex;
};

struct ModelAssetPrimitive {
    // CPU-side primitive payload before upload to RenderModel GPU buffers.
    // Exactly one vertex stream is expected:
    // - vertices for static meshes
    // - skinned_vertices for skinned meshes
    // Importers reject missing material/skin/node references before upload.
    std::vector<Vertex> vertices;
    std::vector<SkinnedVertex> skinned_vertices;
    std::vector<uint32_t> indices;
    uint32_t material = 0;
    uint32_t skin = std::numeric_limits<uint32_t>::max();
    uint32_t deformer = kInvalidModelDeformerIndex;
    int32_t node = -1;
    bool skinned = false;
    bool casts_shadow = true;
    glm::mat4 transform{1.0f};
    Bounds bounds{};

    [[nodiscard]] bool uses_skinned_vertices() const noexcept
    {
        return skinned || !skinned_vertices.empty();
    }

    [[nodiscard]] size_t vertex_count() const noexcept
    {
        return uses_skinned_vertices() ? skinned_vertices.size() : vertices.size();
    }

    [[nodiscard]] size_t index_count() const noexcept
    {
        return indices.size();
    }
};

struct ModelAsset {
    // CPU-owned modern model asset. Source adapters compile glTF/NWN/native data
    // into this shape in memory; a later upload step owns conversion to
    // RenderModel GPU buffers. This is source-agnostic by construction: NWN
    // quirks must already be lowered to sockets, particles, deformers, material
    // fallback flags, or source-side diagnostics before data reaches this record.
    ModelAssetSourceKind source_kind = ModelAssetSourceKind::native;
    std::string name;
    std::vector<Material> materials;
    std::vector<ModelAssetTextureSource> texture_sources;
    std::vector<ModelAssetMaterialTextureSources> material_texture_sources;
    std::vector<ModelAssetPrimitive> primitives;
    std::vector<Node> nodes;
    std::vector<ModelSocket> sockets;
    std::vector<ModelDeformer> deformers;
    std::vector<Skin> skins;
    std::vector<Skeleton> skeletons;
    std::vector<AnimationClip> animations;
    std::vector<ModelAssetParticleSystem> particle_systems;
    ModelShadowSummary shadow{};
    Bounds bounds{};

    [[nodiscard]] bool empty() const noexcept
    {
        return primitives.empty() && particle_systems.empty();
    }
};

struct ModelAssetValidationStats {
    uint32_t primitive_count = 0;
    uint32_t valid_primitive_count = 0;
    uint32_t node_count = 0;
    uint32_t invalid_node_parent_count = 0;
    uint32_t deformer_count = 0;
    uint32_t invalid_deformer_source_node_count = 0;
    uint32_t empty_deformer_vertex_count = 0;
    uint32_t skin_count = 0;
    uint32_t invalid_skin_skeleton_count = 0;
    uint32_t invalid_skin_joint_count = 0;
    uint32_t invalid_skin_inverse_bind_count = 0;
    uint32_t skeleton_count = 0;
    uint32_t invalid_skeleton_joint_parent_count = 0;
    uint32_t invalid_skeleton_joint_node_count = 0;
    uint32_t invalid_skeleton_eval_order_count = 0;
    uint32_t invalid_skeleton_node_to_joint_count = 0;
    uint32_t animation_count = 0;
    uint32_t invalid_animation_skeleton_count = 0;
    uint32_t invalid_animation_track_count = 0;
    uint32_t invalid_animation_duration_count = 0;
    uint32_t empty_vertex_payload_count = 0;
    uint32_t empty_index_payload_count = 0;
    uint32_t index_out_of_range_count = 0;
    uint32_t invalid_material_count = 0;
    uint32_t invalid_skin_count = 0;
    uint32_t unsupported_skin_bone_count = 0;
    uint32_t invalid_node_count = 0;
    uint32_t invalid_deformer_count = 0;
    uint32_t particle_system_count = 0;
    uint32_t texture_source_count = 0;
    uint32_t material_texture_binding_count = 0;
    uint32_t invalid_material_texture_binding_count = 0;

    [[nodiscard]] uint32_t invalid_primitive_count() const noexcept
    {
        return primitive_count - valid_primitive_count;
    }

    [[nodiscard]] uint32_t invalid_asset_row_count() const noexcept
    {
        return invalid_node_parent_count
            + invalid_deformer_source_node_count
            + empty_deformer_vertex_count
            + unsupported_skin_bone_count
            + invalid_skin_skeleton_count
            + invalid_skin_joint_count
            + invalid_skin_inverse_bind_count
            + invalid_skeleton_joint_parent_count
            + invalid_skeleton_joint_node_count
            + invalid_skeleton_eval_order_count
            + invalid_skeleton_node_to_joint_count
            + invalid_animation_skeleton_count
            + invalid_animation_track_count
            + invalid_animation_duration_count;
    }

    [[nodiscard]] bool passed() const noexcept
    {
        return invalid_primitive_count() == 0
            && invalid_asset_row_count() == 0
            && invalid_material_texture_binding_count == 0;
    }
};

// Batch validation for the CPU asset contract. Invalid primitive references are
// counted here so upload/importer stages can drop them before creating GPU
// buffers. This function does not mutate or repair source data.
[[nodiscard]] ModelAssetValidationStats validate_model_asset(const ModelAsset& asset) noexcept;
[[nodiscard]] ModelShadowSummary summarize_model_asset_shadows(const ModelAsset& asset) noexcept;
[[nodiscard]] ModelShadowSummary summarize_render_model_shadows(const RenderModel& model) noexcept;

struct ModelAssetUploadStats {
    uint32_t primitive_count = 0;
    uint32_t uploaded_primitive_count = 0;
    uint32_t invalid_primitive_count = 0;
    uint32_t invalid_asset_row_count = 0;
    uint32_t invalid_material_texture_binding_count = 0;
    uint32_t missing_context_count = 0;
    uint32_t buffer_failure_count = 0;
    uint32_t index16_primitive_count = 0;
    uint32_t index32_primitive_count = 0;

    [[nodiscard]] bool passed() const noexcept
    {
        return primitive_count != 0
            && uploaded_primitive_count == primitive_count
            && invalid_primitive_count == 0
            && invalid_asset_row_count == 0
            && invalid_material_texture_binding_count == 0
            && missing_context_count == 0
            && buffer_failure_count == 0;
    }
};

struct ModelAssetTextureUploadDesc {
    nw::gfx::Context* ctx = nullptr;
    nw::gfx::BindlessTextureIndex fallback_albedo = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex fallback_normal = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex fallback_surface = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex fallback_emissive = nw::gfx::kInvalidBindlessTextureIndex;
};

struct ModelAssetTextureUploadStats {
    uint32_t material_count = 0;
    uint32_t texture_source_count = 0;
    uint32_t material_texture_binding_count = 0;
    uint32_t uploaded_texture_count = 0;
    uint32_t fallback_material_count = 0;
    uint32_t invalid_material_texture_binding_count = 0;
    uint32_t missing_context_count = 0;
    uint32_t missing_source_payload_count = 0;
    uint32_t decode_failure_count = 0;
    uint32_t surface_size_mismatch_count = 0;
    uint32_t texture_create_failure_count = 0;
    uint32_t texture_upload_failure_count = 0;
    uint32_t bindless_failure_count = 0;

    [[nodiscard]] bool passed() const noexcept
    {
        return invalid_material_texture_binding_count == 0
            && missing_context_count == 0
            && missing_source_payload_count == 0
            && decode_failure_count == 0
            && surface_size_mismatch_count == 0
            && texture_create_failure_count == 0
            && texture_upload_failure_count == 0
            && bindless_failure_count == 0;
    }
};

struct ModelAssetDecodedTexture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    [[nodiscard]] bool valid() const noexcept
    {
        return width > 0 && height > 0 && !pixels.empty();
    }
};

struct ModelAssetUploadResult {
    std::unique_ptr<RenderModel> model;
    ModelAssetUploadStats stats;
};

// Bridge helper for existing stream-oriented importers. The complete model
// upload path is upload_model_asset(), which validates and uploads a primitive
// batch. The output primitive must not already own live GPU buffers.
[[nodiscard]] bool upload_model_asset_primitive(nw::gfx::Context* ctx, const ModelAssetPrimitive& source, Primitive& out);

// Uploads a validated CPU ModelAsset into the current RenderModel GPU buffer
// representation. This copies source-agnostic asset metadata and creates
// primitive vertex/index buffers. Texture loading is intentionally outside this
// boundary; material bindless indices are copied as supplied by the source
// adapter. Invalid assets are rejected before GPU work. Missing context or any
// buffer upload failure rejects the whole model and reports the reason in stats.
[[nodiscard]] ModelAssetUploadResult upload_model_asset(const ModelAsset& asset, nw::gfx::Context* ctx);

// Uploads ModelAsset source textures into an existing RenderModel material
// table. Input texture rows are decoded only inside this batch transform; the
// output model owns created GPU textures and receives bindless material indices.
// Bottom-origin TGA rows are restored to file order to match the NWN legacy
// texture path. Existing model textures are preserved. Missing/invalid source
// payloads use supplied fallbacks and increment stats.
[[nodiscard]] ModelAssetTextureUploadStats upload_model_asset_material_textures(
    const ModelAsset& asset, const ModelAssetTextureUploadDesc& desc, RenderModel& model);

// CPU decode step used by the material texture upload batch. Exposed so tests
// can verify source-format policies without requiring a graphics context.
[[nodiscard]] ModelAssetDecodedTexture decode_model_asset_texture_source_rgba8(
    const ModelAssetTextureSource& source, ModelAssetTextureUploadStats& stats);

} // namespace nw::render
