#pragma once

#include <nw/gfx/gfx.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace nw::render {

inline constexpr uint32_t kShadowCascadeCount = 3;

// Cost cap on simultaneously shadow-casting local lights. Each slot is one
// perspective depth map; the shader weights only the casting light's own term,
// so this is a budget, not an aesthetic count (see forward_plus roadmap C4).
inline constexpr uint32_t kLocalShadowCount = 4;

enum class RenderPassSelection {
    opaque_cutout,
    water,
    transparent,
    all,
};

enum class LocalLightContribution : uint32_t {
    diffuse = 0,
    ambient = 1,
};

struct LocalLight {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    LocalLightContribution contribution = LocalLightContribution::diffuse;
    float vertical_scale = 1.0f;
    bool casts_shadow = false;
    // Local-shadow atlas slot assigned by resolve_local_shadows (-1 = unshadowed).
    // Packed into ForwardPlusLightGpu.params.z as slot+1 (0 = none).
    int32_t shadow_slot = -1;
};

struct ForwardPlusLightGpu {
    glm::vec4 position_radius{0.0f};
    glm::vec4 color_intensity{0.0f};
    glm::vec4 params{0.0f};
};

struct ForwardPlusClusterHeader {
    uint32_t offset = 0;
    uint32_t count = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};

struct ForwardPlusStats {
    float light_cull_seconds = 0.0f;
    float cluster_header_seconds = 0.0f;
    float cluster_index_seconds = 0.0f;
    uint32_t light_count = 0;
    uint32_t cluster_count = 0;
    uint32_t active_cluster_count = 0;
    uint32_t cluster_light_index_count = 0;
    uint32_t cluster_light_index_capacity = 0;
    uint32_t max_lights_per_cluster = 0;
    uint32_t overflow_cluster_count = 0;
    uint32_t overflow_light_count = 0;
    uint32_t upload_bytes = 0;
};

enum class ForwardPlusDebugMode : uint32_t {
    off = 0,
    cluster_light_count = 1,
    depth_slice = 2,
};

struct ForwardPlusResources {
    bool enabled = false;
    uint32_t tile_size = 0;
    uint32_t max_lights_per_cluster = 0;
    glm::uvec4 cluster_dimensions{0u};
    glm::vec4 depth_params{0.0f};
    glm::vec4 viewport{0.0f};
    nw::gfx::StorageSpan light_buffer{};
    nw::gfx::StorageSpan cluster_header_buffer{};
    nw::gfx::StorageSpan cluster_light_index_buffer{};
    ForwardPlusStats stats{};
};

static_assert(std::is_standard_layout_v<ForwardPlusLightGpu>);
static_assert(std::is_standard_layout_v<ForwardPlusClusterHeader>);
static_assert(sizeof(ForwardPlusLightGpu) % 16 == 0);
static_assert(sizeof(ForwardPlusClusterHeader) % 16 == 0);

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
    uint32_t two_sided_lighting = 0;

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
    uint32_t ibl_diffuse_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    uint32_t ibl_specular_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    uint32_t ibl_brdf_lut_texture_index = nw::gfx::kInvalidBindlessTextureIndex;

    glm::uvec4 plt_colors0{0u};
    glm::uvec4 plt_colors1{0u};
    glm::uvec4 plt_colors2{0u};

    // xy = normalized wind direction, z = force, w = speed multiplier.
    glm::vec4 environment_wind{0.70710677f, 0.70710677f, 0.05f, 1.0f};
    // x = weather density, y = weather type, z = area flags, w = reserved.
    glm::vec4 environment_weather{0.0f};

    glm::uvec4 forward_plus_cluster_dims{0u};
    glm::uvec4 forward_plus_params{0u};
    glm::vec4 forward_plus_depth_params{0.0f};
    glm::vec4 forward_plus_viewport{0.0f};
    glm::vec4 emissive_color{0.0f};
    // x = roughness, y = legacy specular strength, zw = reserved.
    glm::vec4 material_params{0.78f, 0.12f, 0.0f, 0.0f};
    // x = normal, y = surface/ORM, z = emissive, w = legacy specular map.
    glm::uvec4 material_texture_indices{0u};

    // RenderModel PBR receivers cannot use ShadowConstants at b4 because their
    // SurfaceConstants material already owns that register. These scene-owned
    // fields mirror ShadowConstants for that path; zero texture indices mean
    // "no shadow map" and shader sampling returns fully lit.
    std::array<glm::mat4, kShadowCascadeCount> scene_shadow_world_to_shadow{};
    glm::uvec4 scene_shadow_texture_indices{0u};
    glm::vec4 scene_shadow_split_distances{0.0f};
    float scene_shadow_strength = 1.0f;
    uint32_t scene_shadow_debug_mode = 0;
    glm::vec2 scene_shadow_pad{0.0f};
    std::array<glm::mat4, kLocalShadowCount> scene_local_shadow_world_to_shadow{};
    glm::uvec4 scene_local_shadow_texture_indices{0u};
    // x = strength, y = active count, zw = pad.
    glm::vec4 scene_local_shadow_params{0.0f};
};

static_assert(std::is_standard_layout_v<SceneConstants>);
static_assert(offsetof(SceneConstants, plt_colors0) % 16 == 0);
static_assert(offsetof(SceneConstants, environment_wind) % 16 == 0);
static_assert(offsetof(SceneConstants, forward_plus_cluster_dims) % 16 == 0);
static_assert(sizeof(SceneConstants) % 16 == 0);

[[nodiscard]] inline bool forward_plus_resources_ready(const ForwardPlusResources* forward_plus) noexcept
{
    return forward_plus
        && forward_plus->enabled
        && forward_plus->light_buffer.buffer.valid()
        && forward_plus->cluster_header_buffer.buffer.valid()
        && forward_plus->cluster_light_index_buffer.buffer.valid();
}

[[nodiscard]] inline bool forward_plus_resources_ready(const ForwardPlusResources& forward_plus) noexcept
{
    return forward_plus_resources_ready(&forward_plus);
}

struct ForwardPlusStorageBindings {
    bool ready = false;
    nw::gfx::StorageSpan cluster_headers{};
    nw::gfx::StorageSpan cluster_light_indices{};
    nw::gfx::StorageSpan lights{};
};

[[nodiscard]] inline ForwardPlusStorageBindings make_forward_plus_storage_bindings(
    const ForwardPlusResources* forward_plus,
    nw::gfx::StorageSpan fallback_storage) noexcept
{
    const bool ready = forward_plus_resources_ready(forward_plus);
    return ForwardPlusStorageBindings{
        .ready = ready,
        .cluster_headers = ready ? forward_plus->cluster_header_buffer : fallback_storage,
        .cluster_light_indices = ready ? forward_plus->cluster_light_index_buffer : fallback_storage,
        .lights = ready ? forward_plus->light_buffer : fallback_storage,
    };
}

[[nodiscard]] inline ForwardPlusStorageBindings make_forward_plus_storage_bindings(
    const ForwardPlusResources& forward_plus,
    nw::gfx::StorageSpan fallback_storage) noexcept
{
    return make_forward_plus_storage_bindings(&forward_plus, fallback_storage);
}

inline void apply_forward_plus_scene_constants(
    SceneConstants& constants,
    const ForwardPlusResources& forward_plus,
    ForwardPlusDebugMode debug_mode) noexcept
{
    constants.forward_plus_cluster_dims = glm::uvec4(
        forward_plus.cluster_dimensions.x,
        forward_plus.cluster_dimensions.y,
        forward_plus.cluster_dimensions.z,
        forward_plus.tile_size);
    constants.forward_plus_params = glm::uvec4(
        forward_plus_resources_ready(forward_plus) ? 1u : 0u,
        forward_plus.max_lights_per_cluster,
        forward_plus.stats.light_count,
        static_cast<uint32_t>(debug_mode));
    constants.forward_plus_depth_params = forward_plus.depth_params;
    constants.forward_plus_viewport = forward_plus.viewport;
}

[[nodiscard]] inline uint32_t forward_plus_depth_slice_for_view_depth(
    const ForwardPlusResources& forward_plus,
    float view_depth) noexcept
{
    const uint32_t dim_z = std::max(forward_plus.cluster_dimensions.z, 1u);
    const float near_plane = std::max(forward_plus.depth_params.x, 1.0e-4f);
    const float far_plane = std::max(forward_plus.depth_params.y, near_plane + 1.0f);
    const float depth = std::max(view_depth, near_plane);
    float t = 0.0f;
    if (forward_plus.depth_params.w >= 0.5f) {
        t = (depth - near_plane) / (far_plane - near_plane);
    } else {
        t = std::log(depth / near_plane) / std::max(forward_plus.depth_params.z, 1.0e-4f);
    }
    t = std::clamp(t, 0.0f, 0.999999f);
    return std::min(dim_z - 1u, static_cast<uint32_t>(t * static_cast<float>(dim_z)));
}

[[nodiscard]] inline uint32_t forward_plus_cluster_index_for_screen_position(
    const ForwardPlusResources& forward_plus,
    glm::vec2 screen_position,
    float view_depth) noexcept
{
    const uint32_t dim_x = std::max(forward_plus.cluster_dimensions.x, 1u);
    const uint32_t dim_y = std::max(forward_plus.cluster_dimensions.y, 1u);
    const uint32_t tile_size = std::max(forward_plus.tile_size, 1u);
    const glm::vec2 pixel = glm::max(
        screen_position - glm::vec2{forward_plus.viewport.x, forward_plus.viewport.y},
        glm::vec2{0.0f});
    const uint32_t tile_x = std::min(dim_x - 1u, static_cast<uint32_t>(pixel.x / static_cast<float>(tile_size)));
    const uint32_t tile_y = std::min(dim_y - 1u, static_cast<uint32_t>(pixel.y / static_cast<float>(tile_size)));
    const uint32_t tile_z = forward_plus_depth_slice_for_view_depth(forward_plus, view_depth);
    return (tile_z * dim_y + tile_y) * dim_x + tile_x;
}

enum class SceneWeatherType : uint32_t {
    clear = 0,
    rain = 1,
    snow = 2,
};

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
    uint32_t cascade_count = kShadowCascadeCount;
    std::array<glm::mat4, kShadowCascadeCount> world_to_shadow{};
    std::array<nw::gfx::Handle<nw::gfx::Texture>, kShadowCascadeCount> depth_textures{};
    std::array<float, kShadowCascadeCount> split_distances{};
    float strength = 1.0f;
    uint32_t debug_mode = 0;
};

// Top-K shadow-casting local lights. Each active slot is a single top-down ortho
// depth map aimed from the light toward the scene floor; the shader multiplies
// only that light's contribution by its visibility so N casters self-balance.
struct SceneLocalShadows {
    uint32_t count = 0;
    std::array<glm::mat4, kLocalShadowCount> world_to_shadow{};
    std::array<nw::gfx::Handle<nw::gfx::Texture>, kLocalShadowCount> depth_textures{};
    float strength = 1.0f;
};

struct SceneEnvironment {
    glm::vec2 wind_direction{0.70710677f, 0.70710677f};
    float wind_strength = 0.05f;
    float wind_speed = 1.0f;
    float weather_density = 0.0f;
    SceneWeatherType weather_type = SceneWeatherType::clear;
    uint32_t area_flags = 0;
    float water_quality = 1.0f;
};

struct ShadowConstants {
    std::array<glm::mat4, kShadowCascadeCount> world_to_shadow{};
    glm::uvec4 shadow_texture_indices{0u};
    glm::vec4 shadow_split_distances{0.0f};
    float shadow_strength = 1.0f;
    uint32_t debug_mode = 0;
    glm::vec2 _pad{0.0f};
    // Local-light shadows (Forward+ point/area). Slot i maps to params.z == i+1.
    std::array<glm::mat4, kLocalShadowCount> local_world_to_shadow{};
    glm::uvec4 local_shadow_texture_indices{0u};
    // x = strength, y = active count, zw = pad.
    glm::vec4 local_shadow_params{0.0f};
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
    float time_seconds = 0.0f;
    uint32_t static_pbr_ibl_diffuse_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    uint32_t static_pbr_ibl_specular_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    uint32_t static_pbr_brdf_lut_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    bool static_pbr_ibl_enabled = false;
    Lighting lighting{};
    LightingSpace lighting_space = LightingSpace::camera_relative;
    bool offscreen_pass = false;
    SceneFog fog{};
    SceneShadow shadow{};
    SceneLocalShadows local_shadows{};
    SceneEnvironment environment{};
    std::span<const LocalLight> local_lights;
    ForwardPlusResources forward_plus{};
    ForwardPlusDebugMode forward_plus_debug_mode = ForwardPlusDebugMode::off;
};

[[nodiscard]] inline glm::vec3 render_context_camera_forward(const RenderContext& ctx) noexcept
{
    const glm::vec3 delta = ctx.camera_target - ctx.camera_position;
    if (glm::dot(delta, delta) <= 1.0e-8f) {
        return glm::vec3{0.0f, 1.0f, 0.0f};
    }
    return glm::normalize(delta);
}

[[nodiscard]] inline glm::vec3 encode_scene_light_direction(
    const RenderContext& ctx,
    glm::vec3 direction) noexcept
{
    if (glm::dot(direction, direction) < 1.0e-8f) {
        return glm::vec3{0.0f, 0.0f, 1.0f};
    }

    if (ctx.lighting_space == LightingSpace::world_space) {
        return -glm::normalize(direction);
    }

    const glm::vec3 forward = render_context_camera_forward(ctx);
    glm::vec3 right = glm::cross(forward, glm::vec3{0.0f, 0.0f, 1.0f});
    if (glm::dot(right, right) <= 1.0e-8f) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        right = glm::normalize(right);
    }
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const glm::vec3 world_light = glm::normalize(right * direction.x + (-forward) * direction.y + up * direction.z);
    return -world_light;
}

[[nodiscard]] inline SceneConstants make_scene_constants(
    const RenderContext& ctx,
    const Lighting& lighting) noexcept
{
    SceneConstants constants{};
    constants.view = ctx.view;
    constants.projection = ctx.projection;
    constants.camera_pos = glm::vec4(ctx.camera_position, 0.0f);
    constants.ambient = glm::vec4(lighting.ambient, 0.0f);
    constants.key_dir_intensity = glm::vec4(
        encode_scene_light_direction(ctx, lighting.key_direction),
        lighting.key_intensity);
    constants.key_color = glm::vec4(lighting.key_color, 0.0f);
    constants.fill_dir_intensity = glm::vec4(
        encode_scene_light_direction(ctx, lighting.fill_direction),
        lighting.fill_intensity);
    constants.fill_color = glm::vec4(lighting.fill_color, 0.0f);
    constants.rim_dir_intensity = glm::vec4(
        encode_scene_light_direction(ctx, lighting.rim_direction),
        lighting.rim_intensity);
    constants.rim_color = glm::vec4(lighting.rim_color, 0.0f);
    constants.fog_color = glm::vec4(ctx.fog.color, 0.0f);
    constants.fog_range = glm::vec2(ctx.fog.start_distance, ctx.fog.end_distance);
    constants.fog_amount = ctx.fog.amount;
    constants.fog_enabled = ctx.fog.enabled ? 1u : 0u;
    constants.environment_wind = glm::vec4(
        ctx.environment.wind_direction,
        ctx.environment.wind_strength,
        ctx.environment.wind_speed);
    constants.environment_weather = glm::vec4(
        ctx.environment.weather_density,
        static_cast<float>(ctx.environment.weather_type),
        static_cast<float>(ctx.environment.area_flags),
        ctx.environment.water_quality);
    apply_forward_plus_scene_constants(constants, ctx.forward_plus, ctx.forward_plus_debug_mode);
    constants.ibl_diffuse_texture_index = ctx.static_pbr_ibl_enabled
        ? ctx.static_pbr_ibl_diffuse_texture_index
        : nw::gfx::kInvalidBindlessTextureIndex;
    constants.ibl_specular_texture_index = ctx.static_pbr_ibl_enabled
        ? ctx.static_pbr_ibl_specular_texture_index
        : nw::gfx::kInvalidBindlessTextureIndex;
    constants.ibl_brdf_lut_texture_index = ctx.static_pbr_ibl_enabled
        ? ctx.static_pbr_brdf_lut_texture_index
        : nw::gfx::kInvalidBindlessTextureIndex;
    return constants;
}

[[nodiscard]] inline SceneConstants make_scene_constants(const RenderContext& ctx) noexcept
{
    return make_scene_constants(ctx, ctx.lighting);
}

struct FogCompositeConstants {
    uint32_t color_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    uint32_t depth_texture_index = nw::gfx::kInvalidBindlessTextureIndex;
    float fog_amount = 0.0f;
    float _pad0 = 0.0f;
    glm::vec4 fog_color{0.0f};
    glm::vec2 fog_range{0.0f};
    glm::vec2 camera_clip_planes{0.1f, 1000.0f};
};

static_assert(std::is_standard_layout_v<FogCompositeConstants>);
static_assert(offsetof(FogCompositeConstants, fog_color) == 16);
static_assert(sizeof(FogCompositeConstants) == 48);

// Layout must be kept in sync with the ParticleDrawConstants cbuffer in shaders.
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
    float _pad0 = 0.0f;
    glm::vec4 fog_color{0.0f};
    glm::vec2 fog_range{0.0f};
    float fog_amount = 0.0f;
    uint32_t fog_enabled = 0;
};

static_assert(std::is_standard_layout_v<ParticleDrawConstants>);
static_assert(offsetof(ParticleDrawConstants, fog_color) == 160);
static_assert(sizeof(ParticleDrawConstants) == 192);

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
