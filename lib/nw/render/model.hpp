#pragma once

#include "model_instance_handle.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/render/animation_backend.hpp>
#include <nw/render/particle_def.hpp>

#include <glm/glm.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace nw::render {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec4 tangent;
};

struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec4 tangent;
    uint32_t joint_indices = 0;
    uint32_t joint_weights = 0;
};

// Renderer skinning contract for packed UByte4 joint indices. The current
// shaders index four bone matrices unconditionally, so every packed lane must
// be <= kModelMaxSkinBoneIndex before a skinned vertex buffer reaches the GPU.
inline constexpr std::size_t kModelMaxSkinBones = 64;
inline constexpr uint32_t kModelMaxSkinBoneIndex = static_cast<uint32_t>(kModelMaxSkinBones - 1);

[[nodiscard]] inline bool model_skin_bone_count_supported(std::size_t bone_count) noexcept
{
    return bone_count <= kModelMaxSkinBones;
}

[[nodiscard]] inline uint32_t clamp_model_skin_joint_index(uint32_t joint_index) noexcept
{
    return joint_index <= kModelMaxSkinBoneIndex ? joint_index : kModelMaxSkinBoneIndex;
}

struct Bounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    glm::vec3 center() const noexcept { return (min + max) * 0.5f; }
    float radius() const noexcept { return glm::length(max - min) * 0.5f; }
};

enum class MaterialMode {
    opaque,
    cutout,
    transparent,
    water,
};

enum class ModelAssetSourceKind : uint8_t {
    native,
    gltf,
    nwn_legacy,
};

// Neutral ORM texel for PBR surface maps. The PBR shader multiplies material
// factors by sampled channels, so missing surface textures must preserve
// authored occlusion/roughness/metallic values.
inline constexpr uint8_t kModelSurfaceNeutralOcclusion = 255;
inline constexpr uint8_t kModelSurfaceNeutralRoughness = 255;
inline constexpr uint8_t kModelSurfaceNeutralMetallic = 0;
inline constexpr uint8_t kModelSurfaceNeutralAlpha = 255;

struct Material {
    glm::vec4 albedo{1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float specular_strength = 1.0f;
    glm::vec3 emissive{0.0f};
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float ibl_strength = 1.0f;
    float exposure = 1.0f;

    nw::gfx::BindlessTextureIndex albedo_index = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex normal_index = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::BindlessTextureIndex surface_index = nw::gfx::kInvalidBindlessTextureIndex; // ORM: R=occlusion G=roughness B=metallic
    nw::gfx::BindlessTextureIndex emissive_index = nw::gfx::kInvalidBindlessTextureIndex;

    // Common material protocol for NWN PLT albedo sources.
    // albedo_uses_plt is asset/source metadata: the albedo texture stores
    // raw PLT color-index/layer pairs. plt_enabled is runtime material state:
    // the current instance selected palette rows and the shader should decode.
    bool albedo_uses_plt = false;
    bool plt_enabled = false;
    glm::uvec4 plt_colors0{0u};
    glm::uvec4 plt_colors1{0u};
    glm::uvec4 plt_colors2{0u};

    // Set by importers when an explicitly referenced material payload is missing
    // and existing fallback textures/materials are used instead.
    bool material_uses_fallback = false;
    MaterialMode alpha_mode = MaterialMode::opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
};

inline constexpr uint32_t kInvalidModelMaterialOverrideIndex = std::numeric_limits<uint32_t>::max();

struct ModelMaterialOverrideHandle {
    uint32_t index = kInvalidModelMaterialOverrideIndex;
    uint32_t generation = 0;

    bool operator==(const ModelMaterialOverrideHandle&) const = default;
    auto operator<=>(const ModelMaterialOverrideHandle&) const = default;

    [[nodiscard]] bool valid() const noexcept { return generation != 0 && index != kInvalidModelMaterialOverrideIndex; }
};

struct ModelMaterialOverride {
    Material material;
};

class ModelMaterialOverrideStore {
public:
    [[nodiscard]] ModelMaterialOverrideHandle create(ModelMaterialOverride override)
    {
        uint32_t index = 0;
        if (free_list_head_ != kInvalidModelMaterialOverrideIndex) {
            index = free_list_head_;
            auto& record = records_[index];
            free_list_head_ = record.next_free;
            record.next_free = kInvalidModelMaterialOverrideIndex;
            record.alive = true;
            record.override = std::move(override);
        } else {
            index = static_cast<uint32_t>(records_.size());
            records_.push_back(Record{
                .override = std::move(override),
                .generation = 1,
                .next_free = kInvalidModelMaterialOverrideIndex,
                .alive = true,
            });
        }
        ++live_count_;
        return ModelMaterialOverrideHandle{.index = index, .generation = records_[index].generation};
    }

    void destroy(ModelMaterialOverrideHandle handle) noexcept
    {
        if (!valid(handle)) {
            return;
        }

        auto& record = records_[handle.index];
        record.override = {};
        record.alive = false;
        ++record.generation;
        if (record.generation == 0) {
            record.generation = 1;
        }
        record.next_free = free_list_head_;
        free_list_head_ = handle.index;
        --live_count_;
    }

    [[nodiscard]] ModelMaterialOverride* get(ModelMaterialOverrideHandle handle) noexcept
    {
        return valid(handle) ? &records_[handle.index].override : nullptr;
    }

    [[nodiscard]] const ModelMaterialOverride* get(ModelMaterialOverrideHandle handle) const noexcept
    {
        return valid(handle) ? &records_[handle.index].override : nullptr;
    }

    [[nodiscard]] bool valid(ModelMaterialOverrideHandle handle) const noexcept
    {
        if (!handle.valid()) {
            return false;
        }
        if (handle.index >= records_.size()) {
            return false;
        }
        const auto& record = records_[handle.index];
        return record.alive && record.generation == handle.generation;
    }

    void clear() noexcept
    {
        records_.clear();
        free_list_head_ = kInvalidModelMaterialOverrideIndex;
        live_count_ = 0;
    }

    [[nodiscard]] size_t size() const noexcept { return live_count_; }
    [[nodiscard]] bool empty() const noexcept { return live_count_ == 0; }

private:
    struct Record {
        ModelMaterialOverride override;
        uint32_t generation = 1;
        uint32_t next_free = kInvalidModelMaterialOverrideIndex;
        bool alive = false;
    };

    std::vector<Record> records_;
    uint32_t free_list_head_ = kInvalidModelMaterialOverrideIndex;
    size_t live_count_ = 0;
};

// GPU upload of Material. Must stay in sync with SurfaceConstants cbuffer in pbr_static.ps.hlsl.
struct SurfaceConstants {
    glm::vec4 albedo{1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float specular_strength = 1.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float ibl_strength = 1.0f;
    float exposure = 1.0f;
    float _pad0 = 0.0f;
    glm::vec4 emissive{0.0f};
    uint32_t albedo_index = 0;
    uint32_t normal_index = 0;
    uint32_t surface_index = 0;
    uint32_t emissive_index = 0;
    uint32_t alpha_mode = 0;
    float alpha_cutoff = 0.5f;
    uint32_t double_sided = 0;
    uint32_t plt_enabled = 0;
    glm::uvec4 plt_colors0{0u};
    glm::uvec4 plt_colors1{0u};
    glm::uvec4 plt_colors2{0u};
};

static_assert(sizeof(SurfaceConstants) == 144);
static_assert(std::is_standard_layout_v<SurfaceConstants>);

struct Node {
    int32_t parent = -1;
    glm::mat4 local_transform{1.0f};
    glm::mat4 world_transform{1.0f};
};

inline constexpr uint32_t kInvalidModelNodeIndex = std::numeric_limits<uint32_t>::max();

struct ModelSocket {
    // Stable node index in the source/runtime asset. The bridge uses NWN
    // source node indices; modern compiled assets should use RenderModel node
    // indices. Invalid means the socket is name-only and cannot resolve a
    // transform without a source-specific fallback.
    uint32_t source_node_index = kInvalidModelNodeIndex;
    glm::mat4 local_transform{1.0f};
    glm::mat4 bind_transform{1.0f};
    std::string name;
};

inline constexpr uint32_t kInvalidModelDeformerIndex = std::numeric_limits<uint32_t>::max();

enum class ModelDeformerKind : uint8_t {
    vertex_shader_sway,
    secondary_motion_chain,
    gpu_vertex_sim,
    legacy_reference_cpu,
};

struct ModelDeformer {
    // Asset-facing deformer protocol. The source adapter emits one record per
    // deforming mesh that can be addressed by index; invalid source nodes or
    // empty meshes are dropped before renderer submission. Weight fields are in
    // [0, 1]. Motion values are importer-clamped to non-negative source units.
    ModelDeformerKind kind = ModelDeformerKind::legacy_reference_cpu;
    uint32_t source_node_index = kInvalidModelNodeIndex;
    uint32_t vertex_count = 0;
    glm::vec3 pivot{0.0f};
    glm::vec3 axis{0.0f, 0.0f, 1.0f};
    float amplitude = 0.0f;
    float displacement = 0.0f;
    float period = 0.0f;
    float tightness = 0.0f;
    float phase = 0.0f;
    float weight_min = 0.0f;
    float weight_average = 0.0f;
    float weight_max = 0.0f;
};

struct Skin {
    // Valid renderer-facing skins have at most kModelMaxSkinBones joints. Source
    // importers must reject/downgrade larger skins or clamp packed vertex joint
    // lanes before creating skinned GPU buffers.
    // Multiple skins may share one skeleton while keeping their own inverse-bind
    // matrices.
    uint32_t skeleton = 0;
    std::vector<int32_t> joints;
    std::vector<glm::mat4> inverse_bind_matrices;
};

struct Primitive {
    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t index_stride = 2;
    uint32_t material = 0;
    uint32_t skin = std::numeric_limits<uint32_t>::max();
    uint32_t deformer = kInvalidModelDeformerIndex;
    int32_t node = -1;
    bool skinned = false;
    bool casts_shadow = true;
    glm::mat4 transform{1.0f};
    Bounds bounds;
};

struct ModelAssetParticleEvent {
    float time = 0.0f;
    uint32_t burst_count = 1;
};

struct ModelAssetParticleSystem {
    // Neutral particle payload carried from CPU ModelAsset into runtime
    // RenderModel. animation_name is empty for always-available/default
    // particle effects; non-empty means the event timeline was compiled for
    // that source animation.
    std::string name;
    std::string animation_name;
    ParticleEffectDef effect;
    std::vector<ModelAssetParticleEvent> effect_events;
    float animation_length = 0.0f;
};

struct ModelShadowSummary {
    Bounds bounds{};
    uint32_t caster_count = 0;
    bool casts_shadow = false;
    bool valid = false;
};

struct RenderModel {
    [[nodiscard]] uint32_t socket_index(std::string_view socket_name) const noexcept;
    [[nodiscard]] const ModelSocket* socket(uint32_t index) const noexcept;
    [[nodiscard]] const Node* socket_node(uint32_t index) const noexcept;

    std::vector<Material> materials;
    std::vector<Primitive> primitives;
    std::vector<Node> nodes;
    std::vector<ModelSocket> sockets;
    std::vector<ModelDeformer> deformers;
    std::vector<Skin> skins;
    std::vector<Skeleton> skeletons;
    std::vector<AnimationClip> animations;
    std::vector<ModelAssetParticleSystem> particle_systems;
    std::vector<nw::gfx::Handle<nw::gfx::Texture>> textures; // Owned by the model; fallback textures are external.
    ModelShadowSummary shadow{};
    Bounds bounds;
    ModelAssetSourceKind source_kind = ModelAssetSourceKind::native;
    std::string name;
};

enum class ModelInstanceKind : uint8_t {
    render_model,
    nwn_legacy,
};

struct ModelInstanceShadowSummary {
    Bounds bounds{};
    uint32_t caster_count = 0;
    bool casts_shadow = false;
};

struct ModelInstanceAnimationState {
    std::unique_ptr<AnimationBackend> backend;
    Pose pose;
    std::vector<std::vector<glm::mat4>> skin_matrices;
    uint32_t clip = 0;
    float time = 0.0f;
    bool looping = true;
    bool enabled = false;
};

// Bridge-phase scene/runtime instance record.
//
// New render/runtime code uses this record as the single source of truth for
// placement/root transform, visibility, animation state, world-space current
// bounds, material override handles, and world-space shadow-caster summary.
//
// Existing NWN rendering keeps nw::render::nwn::ModelInstance as a sidecar until
// that subsystem is migrated. During the bridge it owns the legacy node tree,
// PLT/emitter quirks, legacy CPU dangly parity, anchor lookup, source MDL
// references, and compatibility animation fallback behavior. New bridge writes
// must update the common record first and mirror only the fields still required
// by the legacy sidecar.
struct ModelInstance {
    ModelInstanceKind kind = ModelInstanceKind::render_model;
    bool visible = true;
    glm::mat4 root_transform{1.0f};
    Bounds current_bounds{};
    // Indexed by source material index. Invalid entries use the source asset
    // material. Stale handles are ignored and counted at draw collection.
    std::vector<ModelMaterialOverrideHandle> material_override_handles;
    ModelInstanceShadowSummary shadow{};
    // Dense frame cache for attachment consumers. Index is source/runtime node
    // index; matching valid byte is 0 when the adapter has no transform for
    // that row. Bridge adapters populate this from source-specific sidecars;
    // common RenderModel animation sampling writes sampled node world rows here.
    std::vector<glm::mat4> attachment_node_world_transforms;
    std::vector<uint8_t> attachment_node_transform_valid;

    uint32_t render_model_index = kInvalidModelInstanceIndex;
    uint32_t nwn_legacy_model_index = kInvalidModelInstanceIndex;

    ModelInstanceAnimationState animation;
};

[[nodiscard]] inline bool model_instance_node_world_transform(
    const ModelInstance& instance,
    int32_t node_index,
    glm::mat4& out_transform) noexcept
{
    if (node_index < 0) {
        return false;
    }

    const size_t index = static_cast<size_t>(node_index);
    if (index >= instance.attachment_node_world_transforms.size()
        || index >= instance.attachment_node_transform_valid.size()
        || instance.attachment_node_transform_valid[index] == 0u) {
        return false;
    }

    out_transform = instance.attachment_node_world_transforms[index];
    return true;
}

[[nodiscard]] inline glm::mat4 model_instance_primitive_world_transform(
    const ModelInstance* instance,
    const glm::mat4& root_transform,
    const Primitive& primitive) noexcept
{
    glm::mat4 animated_node_world{1.0f};
    if (instance && model_instance_node_world_transform(*instance, primitive.node, animated_node_world)) {
        return animated_node_world;
    }
    return root_transform * primitive.transform;
}

class ModelInstanceStore {
public:
    [[nodiscard]] ModelInstanceHandle create(ModelInstance instance)
    {
        uint32_t index = 0;
        if (free_list_head_ != kInvalidModelInstanceIndex) {
            index = free_list_head_;
            auto& record = records_[index];
            free_list_head_ = record.next_free;
            record.next_free = kInvalidModelInstanceIndex;
            record.alive = true;
            record.instance = std::move(instance);
        } else {
            index = static_cast<uint32_t>(records_.size());
            records_.push_back(Record{
                .instance = std::move(instance),
                .generation = 1,
                .next_free = kInvalidModelInstanceIndex,
                .alive = true,
            });
        }
        ++live_count_;
        return ModelInstanceHandle{.index = index, .generation = records_[index].generation};
    }

    void destroy(ModelInstanceHandle handle) noexcept
    {
        if (!valid(handle)) {
            return;
        }

        auto& record = records_[handle.index];
        record.instance = {};
        record.alive = false;
        ++record.generation;
        if (record.generation == 0) {
            record.generation = 1;
        }
        record.next_free = free_list_head_;
        free_list_head_ = handle.index;
        --live_count_;
    }

    [[nodiscard]] ModelInstance* get(ModelInstanceHandle handle) noexcept
    {
        return valid(handle) ? &records_[handle.index].instance : nullptr;
    }

    [[nodiscard]] const ModelInstance* get(ModelInstanceHandle handle) const noexcept
    {
        return valid(handle) ? &records_[handle.index].instance : nullptr;
    }

    [[nodiscard]] bool valid(ModelInstanceHandle handle) const noexcept
    {
        if (!handle.valid()) {
            return false;
        }
        if (handle.index >= records_.size()) {
            return false;
        }
        const auto& record = records_[handle.index];
        return record.alive && record.generation == handle.generation;
    }

    void clear() noexcept
    {
        records_.clear();
        free_list_head_ = kInvalidModelInstanceIndex;
        live_count_ = 0;
    }

    [[nodiscard]] size_t size() const noexcept { return live_count_; }
    [[nodiscard]] bool empty() const noexcept { return live_count_ == 0; }

private:
    struct Record {
        ModelInstance instance;
        uint32_t generation = 1;
        uint32_t next_free = kInvalidModelInstanceIndex;
        bool alive = false;
    };

    std::vector<Record> records_;
    uint32_t free_list_head_ = kInvalidModelInstanceIndex;
    size_t live_count_ = 0;
};

} // namespace nw::render
