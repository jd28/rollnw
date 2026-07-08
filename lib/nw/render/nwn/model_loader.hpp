#pragma once

#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/render/animation.hpp>
#include <nw/render/animation_backend.hpp>
#include <nw/render/model.hpp>
#include <nw/render/model_asset.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nw::render::nwn {

using Bounds = nw::render::Bounds;
using Vertex = nw::render::Vertex;

// Vertex format for skinned meshes
struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec4 tangent;
    uint32_t joint_indices;
    uint32_t joint_weights;
};

// Forward declarations
struct Node;
struct Mesh;
struct ModelInstance;

using MaterialMode = nw::render::MaterialMode;

// Base node - can be a bone or a mesh attachment point
struct Node {
    virtual ~Node() = default;

    // Get world transform by walking up parent chain
    glm::mat4 get_transform() const;
    glm::mat4 get_local_transform() const;
    const glm::mat4& refresh_model_transform_cache(const glm::mat4& parent_transform, uint64_t parent_revision) const;
    const glm::mat4& refresh_model_transform_cache() const;
    [[nodiscard]] uint64_t model_transform_cache_revision() const noexcept { return cached_model_transform_revision_; }

    // Hierarchy
    ModelInstance* owner_ = nullptr;
    const nw::model::Node* orig_ = nullptr;
    Node* parent_ = nullptr;
    std::vector<Node*> children_; // Non-owning, owned by ModelInstance::nodes_

    // Transform (updated by animation)
    glm::vec3 position_{0.0f};
    glm::quat rotation_{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale_{1.0f};
    glm::mat4 bind_pose_{1.0f};
    bool has_transform_ = false;
    bool is_mesh = false;
    bool is_skin = false;

    mutable glm::mat4 cached_model_transform_{1.0f};
    mutable glm::vec3 cached_position_{0.0f};
    mutable glm::quat cached_rotation_{1.0f, 0.0f, 0.0f, 0.0f};
    mutable glm::vec3 cached_scale_{1.0f};
    mutable uint64_t cached_parent_model_transform_revision_ = 0;
    mutable uint64_t cached_model_transform_revision_ = 0;
    mutable bool cached_has_transform_ = false;
    mutable bool cached_model_transform_valid_ = false;
};

// Mesh node - renders geometry
struct Mesh : public Node {
    Mesh() noexcept { is_mesh = true; }
    ~Mesh();

    // GPU resources
    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    bool owns_gpu_buffers = true;
    nw::gfx::Handle<nw::gfx::Texture> texture;
    nw::gfx::BindlessTextureIndex texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::Handle<nw::gfx::Texture> normal_texture;
    nw::gfx::BindlessTextureIndex normal_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::Handle<nw::gfx::Texture> surface_texture;
    nw::gfx::BindlessTextureIndex surface_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::Handle<nw::gfx::Texture> emissive_texture;
    nw::gfx::BindlessTextureIndex emissive_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    nw::gfx::Handle<nw::gfx::Texture> specular_texture;
    nw::gfx::BindlessTextureIndex specular_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    std::string bitmap_name;
    std::string normal_map_name;
    std::string specular_map_name;
    std::string roughness_map_name;
    std::string emissive_map_name;
    // Set when an explicit MDL/MTR material resource reference resolves through
    // the renderer fallback texture/material path.
    bool material_uses_fallback = false;
    nw::PltColors plt_colors{};
    bool uses_plt = false;
    std::string renderhint;
    std::string materialname;
    Bounds local_bounds{};
    int transparencyhint = 0;
    MaterialMode material_mode = MaterialMode::opaque;
    float opacity = 1.0f;
    float alpha_cutout_threshold = 0.5f;
    glm::vec3 color_key{0.0f};
    float color_key_threshold = 0.0f;
    glm::vec3 emissive{0.0f};
    float roughness = 0.78f;
    // Legacy sidecar roughness keeps NWN shininess/specular compatibility;
    // common ModelAsset/PBR lowering uses this conservative authored factor.
    float common_pbr_roughness = 0.78f;
    float specular_strength = 0.12f;
    bool is_ground_overlay = false;
    bool two_sided_lighting = false;

    const glm::mat4& refresh_normal_matrix_cache(
        const glm::mat4& world_transform, uint64_t model_revision, uint64_t root_revision) const;

    mutable glm::mat4 cached_normal_matrix_{1.0f};
    mutable uint64_t cached_normal_model_transform_revision_ = 0;
    mutable uint64_t cached_normal_root_transform_revision_ = 0;
    mutable bool cached_normal_matrix_valid_ = false;
};

struct SkinMesh : public Mesh {
    SkinMesh() noexcept { is_skin = true; }

    std::array<int16_t, nw::render::kModelMaxSkinBones> bone_nodes{};
    std::array<glm::mat4, nw::render::kModelMaxSkinBones> inverse_bind_pose_{};

    void initialize_skinning();
    void fill_skin_matrices(glm::mat4* out, size_t count) const;
};

enum class NwnMaterialAlphaProfile {
    opaque,
    binary,
    graded,
};

struct NwnMaterialClassificationInput {
    const nw::model::TrimeshNode* node = nullptr;
    std::string_view bitmap_name;
    nw::model::ModelClass model_class = nw::model::ModelClass::invalid;
    NwnMaterialAlphaProfile alpha_profile = NwnMaterialAlphaProfile::opaque;
    bool has_color_key = false;
    bool has_txi = false;
    std::string_view txi_blending;
    bool txi_decal = false;
};

enum class DanglyMode {
    legacy,
    modern,
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
    uint32_t deformer_count = 0;
    uint32_t secondary_motion_deformer_count = 0;
    uint32_t legacy_reference_deformer_count = 0;
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
    legacy_spring_cpu,
    foliage_sway_cpu,
};

[[nodiscard]] DanglyDeformPolicy dangly_deform_policy_for(const nw::model::DanglymeshNode* node);
[[nodiscard]] std::string_view dangly_deform_policy_name(DanglyDeformPolicy policy) noexcept;
[[nodiscard]] nw::render::ModelDeformerKind model_deformer_kind_for(DanglyDeformPolicy policy) noexcept;
[[nodiscard]] MaterialMode classify_nwn_material(const NwnMaterialClassificationInput& input);

struct DanglyMesh : public Mesh {
    std::vector<Vertex> cpu_vertices_;
    std::vector<glm::vec3> rest_positions_;
    std::vector<glm::vec3> offsets_;
    std::vector<glm::vec3> velocities_;
    std::vector<float> freedom_;

    glm::vec3 pinned_center_{0.0f};
    glm::vec3 loose_center_{0.0f};

    DanglyDeformPolicy deform_policy_ = DanglyDeformPolicy::legacy_spring_cpu;
    uint32_t deformer_index_ = nw::render::kInvalidModelDeformerIndex;
    glm::vec3 foliage_pivot_{0.0f};
    glm::vec3 foliage_axis_{0.0f, 0.0f, 1.0f};
    float foliage_extent_ = 1.0f;
    float foliage_amplitude_ = 0.05f;
    bool constraints_valid_ = false;

    float displacement_ = 0.0f;
    float period_ = 0.0f;
    float tightness_ = 0.0f;
    float time_ = 0.0f;
    float phase_ = 0.0f;

    glm::vec3 previous_world_origin_{0.0f};
    bool has_previous_world_origin_ = false;

    [[nodiscard]] bool uses_foliage_sway() const noexcept;
    [[nodiscard]] nw::render::ModelDeformer make_deformer_record(uint32_t source_node_index) const noexcept;

    void initialize_dangly(const nw::model::DanglymeshNode* node);
    void initialize_dangly(const nw::model::DanglymeshNode* node, DanglyDeformPolicy policy);
    void update_dangly(const glm::mat4& world_transform, int32_t dt_ms);
    void update_legacy_foliage(const glm::mat4& world_transform, int32_t dt_ms);
    void update_modern_foliage(const glm::mat4& world_transform, int32_t dt_ms);
    void recompute_normals();
    void upload_vertices() const;
};

// Model - owns all nodes and manages animation
struct ModelInstance {
    nw::gfx::Context* ctx_ = nullptr;
    std::unique_ptr<nw::model::Mdl> owned_mdl_;

    // Source model data
    const nw::model::Mdl* mdl_ = nullptr;
    const nw::model::Mdl* animation_source_ = nullptr;
    const nw::model::Animation* anim_ = nullptr;
    int32_t anim_cursor_ = 0; // milliseconds

    // Node ownership (flat array, owns all nodes)
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<Node*> source_nodes_;
    std::vector<nw::render::ModelSocket> sockets_;
    std::vector<nw::render::ModelDeformer> deformers_;
    std::vector<Mesh*> shadow_casters_;

    // Stats for main.cpp
    size_t vertex_count = 0;
    size_t index_count = 0;
    Bounds bounds;
    bool render_enabled = true;
    bool scene_animation_enabled = true;
    uint8_t material_pass_mask = 0;
    bool anchor_uses_root_bind_offset = true;
    bool anchor_position_only = false;
    bool accepts_external_animation_source = true;
    float local_scale_ = 1.0f;
    glm::mat4 placement_transform_{1.0f};

    const ModelInstance* transform_context_ = nullptr;
    std::string transform_anchor_;
    std::string transform_source_anchor_;
    uint32_t transform_anchor_socket_index_ = nw::render::kInvalidModelNodeIndex;
    uint32_t transform_source_anchor_socket_index_ = nw::render::kInvalidModelNodeIndex;

    // Ozz animation backend for NWN transform animation (skinned and static paths)
    std::unique_ptr<nw::render::AnimationBackend> nwn_backend_;
    nw::render::Skeleton nwn_skeleton_;
    std::vector<nw::render::AnimationClip> nwn_clips_;
    nw::render::Pose nwn_pose_;
    int32_t nwn_clip_index_ = -1;
    std::unordered_map<std::string, uint32_t> nwn_clip_name_to_index_;
    std::vector<int32_t> joint_to_source_node_; // joint_idx → source_node_idx in mdl
    std::vector<Node*> joint_target_nodes_;
    const nw::model::Mdl* nwn_skeleton_source_ = nullptr;
    std::vector<uint8_t> nwn_clip_has_translation_;
    std::vector<uint8_t> nwn_clip_has_rotation_;
    std::vector<uint8_t> nwn_clip_has_scale_;

    mutable glm::mat4 cached_root_transform_{1.0f};
    mutable glm::mat4 cached_root_normal_matrix_{1.0f};
    mutable uint64_t cached_root_transform_revision_ = 0;
    mutable uint64_t cached_root_normal_transform_revision_ = 0;
    mutable bool cached_root_transform_valid_ = false;
    mutable bool cached_root_normal_matrix_valid_ = false;

    // Find node by name (for animation)
    Node* find(std::string_view name);
    const Node* find(std::string_view name) const;
    [[nodiscard]] uint32_t socket_index(std::string_view name) const noexcept;
    [[nodiscard]] const nw::render::ModelSocket* socket(uint32_t index) const noexcept;
    [[nodiscard]] const Node* socket_node(uint32_t index) const noexcept;
    [[nodiscard]] const std::vector<nw::render::ModelSocket>& sockets() const noexcept { return sockets_; }
    [[nodiscard]] const nw::render::ModelDeformer* deformer(uint32_t index) const noexcept;
    [[nodiscard]] const Node* deformer_node(uint32_t index) const noexcept;
    [[nodiscard]] const std::vector<nw::render::ModelDeformer>& deformers() const noexcept { return deformers_; }

    // Load from NWN model
    bool load(const nw::model::Mdl* mdl, nw::gfx::Context* ctx, std::string_view root_name = {});

    // Load animation
    bool load_animation(std::string_view name);
    void set_animation_source(const nw::model::Mdl* mdl) { animation_source_ = mdl; }
    void set_placement_transform(const glm::mat4& transform) { placement_transform_ = transform; }
    void set_transform_anchor(const ModelInstance* ctx, std::string_view anchor,
        std::string_view source_anchor = {})
    {
        transform_context_ = ctx;
        transform_anchor_ = std::string(anchor);
        transform_source_anchor_ = std::string(source_anchor);
        transform_anchor_socket_index_ = ctx ? ctx->socket_index(anchor) : nw::render::kInvalidModelNodeIndex;
        transform_source_anchor_socket_index_ = source_anchor.empty()
            ? nw::render::kInvalidModelNodeIndex
            : socket_index(source_anchor);
    }
    glm::mat4 root_transform() const;
    [[nodiscard]] uint64_t root_transform_cache_revision(const glm::mat4& model_root) const;
    [[nodiscard]] const glm::mat4& refresh_root_normal_matrix_cache(
        const glm::mat4& model_root, uint64_t root_revision) const;

    // Update animation (dt in milliseconds, matching the viewer frame clock)
    void update(int32_t dt_ms);
    Bounds current_bounds() const;
    Bounds current_bounds(const glm::mat4& root) const;
    [[nodiscard]] bool has_opaque_cutout_pass() const noexcept { return (material_pass_mask & 0x1u) != 0; }
    [[nodiscard]] bool has_water_pass() const noexcept { return (material_pass_mask & 0x2u) != 0; }
    [[nodiscard]] bool has_transparent_pass() const noexcept { return (material_pass_mask & 0x4u) != 0; }
    [[nodiscard]] bool has_shadow_casters() const noexcept { return !shadow_casters_.empty(); }
    [[nodiscard]] const std::vector<Mesh*>& shadow_casters() const noexcept { return shadow_casters_; }

    Node* node_from_source_index(int32_t idx) const;

private:
    Node* load_node(const nw::model::Node* node, Node* parent);
    void capture_bind_pose();
    void build_socket_records();
    void build_deformer_records();
    void build_ozz_data(const nw::model::Mdl* skeleton_source = nullptr);
};

// Loader class
class ModelLoader {
public:
    explicit ModelLoader(nw::gfx::Context* ctx);
    std::unique_ptr<ModelInstance> load(const nw::model::Mdl* mdl, std::string_view root_name = {});
    std::unique_ptr<ModelInstance> load(std::string_view resref, std::string_view root_name = {});

private:
    nw::gfx::Context* ctx_ = nullptr;
    bool create_mesh_buffers(Mesh& mesh, const nw::model::TrimeshNode* node);
    bool create_skin_buffers(SkinMesh& mesh, const nw::model::SkinNode* node);
};

// Helper function
std::unique_ptr<ModelInstance> load_model(std::string_view resref, std::string_view root_name = {});
[[nodiscard]] NwnModelAssetImportResult import_nwn_model_asset(const nw::model::Mdl& mdl);
[[nodiscard]] NwnRenderModelImportResult import_nwn_render_model(
    const nw::model::Mdl& mdl, const nw::render::ModelAssetTextureUploadDesc& texture_upload);
void set_dangly_debug_scale(float scale);
float dangly_debug_scale();
void set_dangly_mode(DanglyMode mode);
DanglyMode dangly_mode();
void clear_model_loader_resource_caches();
[[nodiscard]] ModelLoaderResourceCacheStats model_loader_resource_cache_stats();

} // namespace nw::render::nwn
