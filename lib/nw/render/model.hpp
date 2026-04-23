#pragma once

#include <nw/gfx/gfx.hpp>
#include <nw/render/animation.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
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
};

struct Material {
    glm::vec4 albedo{1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    glm::vec3 emissive{0.0f};
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float ibl_strength = 1.0f;
    float exposure = 1.0f;

    nw::gfx::BindlessTextureIndex albedo_index = 0;
    nw::gfx::BindlessTextureIndex normal_index = 0;
    nw::gfx::BindlessTextureIndex surface_index = 0; // ORM: R=occlusion G=roughness B=metallic
    nw::gfx::BindlessTextureIndex emissive_index = 0;

    MaterialMode alpha_mode = MaterialMode::opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
};

// GPU upload of Material. Must stay in sync with SurfaceConstants cbuffer in pbr_static.ps.hlsl.
struct SurfaceConstants {
    glm::vec4 albedo{1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float ibl_strength = 1.0f;
    float exposure = 1.0f;
    float _pad0[2]{};
    glm::vec4 emissive{0.0f};
    uint32_t albedo_index = 0;
    uint32_t normal_index = 0;
    uint32_t surface_index = 0;
    uint32_t emissive_index = 0;
    uint32_t alpha_mode = 0;
    float alpha_cutoff = 0.5f;
    uint32_t double_sided = 0;
    uint32_t _pad1 = 0;
};

static_assert(sizeof(SurfaceConstants) == 96);
static_assert(std::is_standard_layout_v<SurfaceConstants>);

struct Node {
    int32_t parent = -1;
    glm::mat4 local_transform{1.0f};
    glm::mat4 world_transform{1.0f};
};

struct Skin {
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
    int32_t node = -1;
    bool skinned = false;
    glm::mat4 transform{1.0f};
    Bounds bounds;
};

struct RenderModel {
    std::vector<Material> materials;
    std::vector<Primitive> primitives;
    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<Skeleton> skeletons;
    std::vector<AnimationClip> animations;
    std::vector<nw::gfx::Handle<nw::gfx::Texture>> textures; // Owned by the model; fallback textures are external.
    Bounds bounds;
    std::string name;
};

} // namespace nw::render
