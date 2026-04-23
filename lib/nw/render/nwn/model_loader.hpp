#pragma once

#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/model/Mdl.hpp>
#include <nw/render/animation.hpp>
#include <nw/render/animation_backend.hpp>
#include <nw/render/model.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <memory>
#include <string>
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
};

// Mesh node - renders geometry
struct Mesh : public Node {
    Mesh() noexcept { is_mesh = true; }
    ~Mesh();

    // GPU resources
    nw::gfx::Handle<nw::gfx::Buffer> vertices;
    nw::gfx::Handle<nw::gfx::Buffer> indices;
    uint32_t index_count = 0;
    nw::gfx::Handle<nw::gfx::Texture> texture;
    nw::gfx::BindlessTextureIndex texture_index = 0;
    std::string bitmap_name;
    nw::PltColors plt_colors{};
    bool uses_plt = false;
    std::string renderhint;
    std::string materialname;
    int transparencyhint = 0;
    MaterialMode material_mode = MaterialMode::opaque;
    float opacity = 1.0f;
    float alpha_cutout_threshold = 0.5f;
    glm::vec3 color_key{0.0f};
    float color_key_threshold = 0.0f;
    bool is_ground_overlay = false;
};

struct SkinMesh : public Mesh {
    SkinMesh() noexcept { is_skin = true; }

    std::array<int16_t, 64> bone_nodes{};
    std::array<glm::mat4, 64> inverse_bind_pose_{};

    void initialize_skinning();
    void fill_skin_matrices(glm::mat4* out, size_t count) const;
};

enum class DanglyMode {
    legacy,
    modern,
};

struct DanglyMesh : public Mesh {
    std::vector<Vertex> cpu_vertices_;
    std::vector<glm::vec3> rest_positions_;
    std::vector<glm::vec3> offsets_;
    std::vector<glm::vec3> velocities_;
    std::vector<float> freedom_;

    glm::vec3 pinned_center_{0.0f};
    glm::vec3 loose_center_{0.0f};

    bool is_foliage_like_ = false;
    glm::vec3 foliage_pivot_{0.0f};
    glm::vec3 foliage_axis_{0.0f, 0.0f, 1.0f};
    float foliage_extent_ = 1.0f;
    float foliage_amplitude_ = 0.05f;

    float displacement_ = 0.0f;
    float period_ = 0.0f;
    float tightness_ = 0.0f;
    float time_ = 0.0f;
    float phase_ = 0.0f;

    glm::vec3 previous_world_origin_{0.0f};
    bool has_previous_world_origin_ = false;

    void initialize_dangly(const nw::model::DanglymeshNode* node);
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

    // Stats for main.cpp
    size_t vertex_count = 0;
    size_t index_count = 0;
    Bounds bounds;
    bool render_enabled = true;
    bool scene_animation_enabled = true;
    bool anchor_uses_root_bind_offset = true;
    bool anchor_position_only = false;
    bool accepts_external_animation_source = true;
    float local_scale_ = 1.0f;
    glm::mat4 placement_transform_{1.0f};

    const ModelInstance* transform_context_ = nullptr;
    std::string transform_anchor_;
    std::string transform_source_anchor_;

    // Ozz animation backend for NWN transform animation (skinned and static paths)
    std::unique_ptr<nw::render::AnimationBackend> nwn_backend_;
    nw::render::Skeleton nwn_skeleton_;
    std::vector<nw::render::AnimationClip> nwn_clips_;
    nw::render::Pose nwn_pose_;
    int32_t nwn_clip_index_ = -1;
    std::unordered_map<std::string, uint32_t> nwn_clip_name_to_index_;
    std::vector<int32_t> joint_to_source_node_; // joint_idx → source_node_idx in mdl

    // Find node by name (for animation)
    Node* find(std::string_view name);
    const Node* find(std::string_view name) const;

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
    }
    glm::mat4 root_transform() const;

    // Update animation (dt in milliseconds, like arclight)
    void update(int32_t dt_ms);
    Bounds current_bounds() const;

    Node* node_from_source_index(int32_t idx) const;

private:
    Node* load_node(const nw::model::Node* node, Node* parent);
    void capture_bind_pose();
    void build_ozz_data();
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
void set_dangly_debug_scale(float scale);
float dangly_debug_scale();
void set_dangly_mode(DanglyMode mode);
DanglyMode dangly_mode();

} // namespace nw::render::nwn
