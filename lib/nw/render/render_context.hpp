#pragma once

#include <nw/gfx/gfx.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nw::render {

inline constexpr uint32_t kShadowCascadeCount = 3;

// Shared per-frame/per-draw constants for all mesh pipelines.
// Layout must be kept in sync with the SceneConstants cbuffer in shaders.
struct SceneConstants {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 model;         // world transform for trimesh; identity for skinned
    glm::mat4 normal_matrix; // inverse-transpose of model

    uint32_t texture_index = 0;
    uint32_t alpha_cutout = 0;
    float alpha_cutout_threshold = 0.5f;
    float _pad_tex = 0.0f;

    glm::vec2 color_key_rg{0.0f};
    float color_key_b = 0.0f;
    float color_key_threshold = 0.0f;
    glm::vec4 pad_alpha{0.0f};

    glm::vec4 camera_pos{0.0f};

    glm::vec4 ambient{0.0f};

    glm::vec4 key_dir_intensity{0.0f};
    glm::vec4 key_color{0.0f};

    glm::vec4 fill_dir_intensity{0.0f};
    glm::vec4 fill_color{0.0f};

    glm::vec4 rim_dir_intensity{0.0f};
    glm::vec4 rim_color{0.0f};

    glm::vec4 fog_color{0.0f};
    glm::vec2 fog_range{0.0f};
    float fog_amount = 0.0f;
    uint32_t fog_enabled = 0;

    uint32_t plt_enabled = 0;
    uint32_t ibl_diffuse_texture_index = 0;
    uint32_t ibl_specular_texture_index = 0;
    uint32_t ibl_brdf_lut_texture_index = 0;

    glm::uvec4 plt_colors0{0u};
    glm::uvec4 plt_colors1{0u};
    glm::uvec4 plt_colors2{0u};
};

static_assert(std::is_standard_layout_v<SceneConstants>);
static_assert(offsetof(SceneConstants, plt_colors0) % 16 == 0);

enum class LightingSpace {
    camera_relative,
    world_space,
};

// 3-point lighting in camera-local basis.
// x = camera right, y = toward camera, z = camera up.
// Directions here represent the actual light direction L used by shading before the
// shader converts from stored `*_dir` back to `L = normalize(-dir)`.
struct Lighting {
    glm::vec3 key_direction{-0.35f, 0.9f, 0.35f};
    glm::vec3 key_color{1.0f, 0.95f, 0.9f};
    float key_intensity = 0.9f;

    glm::vec3 fill_direction{0.7f, 0.45f, 0.15f};
    glm::vec3 fill_color{0.58f, 0.64f, 0.76f};
    float fill_intensity = 0.18f;

    glm::vec3 rim_direction{0.0f, -1.0f, 0.25f};
    glm::vec3 rim_color{1.0f, 1.0f, 1.0f};
    float rim_intensity = 0.16f;

    glm::vec3 ambient{0.018f, 0.018f, 0.022f};
};

struct SceneFog {
    bool enabled = false;
    glm::vec3 color{0.0f};
    float amount = 0.0f;
    float start_distance = 0.0f;
    float end_distance = 0.0f;
};

struct SceneShadow {
    bool enabled = false;
    std::array<glm::mat4, kShadowCascadeCount> world_to_shadow{};
    std::array<nw::gfx::Handle<nw::gfx::Texture>, kShadowCascadeCount> depth_textures{};
    std::array<float, kShadowCascadeCount> split_distances{};
    float strength = 1.0f;
    uint32_t debug_mode = 0;
};

struct ShadowConstants {
    std::array<glm::mat4, kShadowCascadeCount> world_to_shadow{};
    glm::uvec4 shadow_texture_indices{0u};
    glm::vec4 shadow_split_distances{0.0f};
    float shadow_strength = 1.0f;
    uint32_t debug_mode = 0;
    glm::vec2 _pad{0.0f};
};

struct RenderContext {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 camera_position;
    glm::vec3 camera_target;
    float camera_near_plane = 0.1f;
    float camera_far_plane = 1000.0f;
    bool orthographic_camera = false;
    float static_pbr_ibl_strength = 1.0f;
    float static_pbr_exposure = 1.0f;
    uint32_t static_pbr_ibl_diffuse_texture_index = 0;
    uint32_t static_pbr_ibl_specular_texture_index = 0;
    uint32_t static_pbr_brdf_lut_texture_index = 0;
    bool static_pbr_ibl_enabled = false;
    Lighting lighting{};
    LightingSpace lighting_space = LightingSpace::camera_relative;
    bool offscreen_pass = false;
    SceneFog fog{};
    SceneShadow shadow{};
};

struct FogCompositeConstants {
    uint32_t color_texture_index = 0;
    uint32_t depth_texture_index = 0;
    float fog_amount = 0.0f;
    float _pad0 = 0.0f;
    glm::vec4 fog_color{0.0f};
    glm::vec2 fog_range{0.0f};
    glm::vec2 camera_clip_planes{0.1f, 1000.0f};
};

struct ParticleDrawConstants {
    glm::mat4 view;
    glm::mat4 projection;
    uint32_t texture_index = 0;
    uint32_t alpha_cutout = 0;
    uint32_t additive_like = 0;
    uint32_t additive_alpha_gated = 0;
    float alpha_cutout_threshold = 0.5f;
    float additive_intensity = 1.0f;
    float alpha_intensity = 1.0f;
    glm::vec4 fog_color{0.0f};
    glm::vec2 fog_range{0.0f};
    float fog_amount = 0.0f;
    uint32_t fog_enabled = 0;
};

struct ParticleVertex {
    glm::vec3 position;
    glm::vec2 texcoord;
    glm::vec4 color;
};

struct ParticleBillboardVertex {
    glm::vec2 corner;
    glm::vec2 texcoord;
};

struct ParticleBillboardInstance {
    glm::vec4 center;
    glm::vec4 right;
    glm::vec4 up;
    glm::vec4 texcoord_rect;
    glm::vec4 color;
};

} // namespace nw::render
