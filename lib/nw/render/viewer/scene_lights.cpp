#include "scene_lights.hpp"

#include "preview_object.hpp"
#include "preview_scene.hpp"
#include "scene_debug.hpp"
#include "tile_light.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/log.hpp>
#include <nw/render/render_context.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace nw::render::viewer {

namespace {

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value
        && (std::strcmp(value, "1") == 0
            || std::strcmp(value, "true") == 0
            || std::strcmp(value, "TRUE") == 0
            || std::strcmp(value, "yes") == 0
            || std::strcmp(value, "YES") == 0
            || std::strcmp(value, "on") == 0
            || std::strcmp(value, "ON") == 0);
}

bool viewer_tile_light_debug_shapes_enabled()
{
    static const bool enabled = environment_flag_enabled("ROLLNW_VIEWER_TILE_LIGHT_DEBUG");
    return enabled;
}

struct SceneModelLightAppendOptions {
    glm::mat4 root_transform{1.0f};
    SceneTileLightSlots tile_slots{};
    int tile_x = -1;
    int tile_y = -1;
    uint8_t tile_orientation = 0;
    SceneLocalLightSource source = SceneLocalLightSource::authored_model;
    uint32_t model_index = kInvalidSceneLocalLightModelIndex;
    int32_t model_source_node_index = -1;
    SceneLocalLightTuning tuning{};
};

float color_max_channel(const glm::vec3& color) noexcept
{
    return std::max(color.x, std::max(color.y, color.z));
}

glm::vec3 tile_light_hue(uint8_t index)
{
    static const std::array<glm::vec3, 7> colors{
        glm::vec3{1.0f, 0.82f, 0.32f},
        glm::vec3{0.32f, 1.0f, 0.36f},
        glm::vec3{0.25f, 0.95f, 1.0f},
        glm::vec3{0.34f, 0.52f, 1.0f},
        glm::vec3{0.72f, 0.38f, 1.0f},
        glm::vec3{1.0f, 0.32f, 0.28f},
        glm::vec3{1.0f, 0.58f, 0.22f},
    };
    return colors[std::min<size_t>(index, colors.size() - 1)];
}

glm::vec3 tile_main_light_debug_color(uint8_t color_id)
{
    static constexpr std::array<float, 4> white_strengths{0.0f, 0.45f, 0.76f, 1.0f};
    static constexpr std::array<float, 4> hue_strengths{0.36f, 0.56f, 0.78f, 1.0f};
    if (color_id <= 3) {
        return glm::vec3{white_strengths[color_id]};
    }

    const uint8_t hue_index = static_cast<uint8_t>((color_id - 4) / 4);
    const uint8_t strength_index = static_cast<uint8_t>((color_id - 4) % 4);
    return tile_light_hue(hue_index) * hue_strengths[strength_index];
}

bool has_visible_light_color(const glm::vec3& color) noexcept
{
    return color_max_channel(color) > 1.0e-4f;
}

SceneLocalLightTuning scene_authored_model_light_tuning(const PreviewScene& scene) noexcept
{
    if (!scene.is_area) {
        return SceneLocalLightTuning{.radius_scale = 1.0f, .intensity_scale = 0.55f};
    }
    return scene_local_light_tuning(scene);
}

SceneLocalLightTuning scene_placeable_table_light_tuning(const PreviewScene& scene) noexcept
{
    if (!scene.is_area) {
        return SceneLocalLightTuning{.radius_scale = 1.0f, .intensity_scale = 1.0f};
    }

    const bool interior = (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool underground = (scene.area_flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    const bool night = scene.area_weather.is_night != 0 || underground;
    if (interior || underground || night) {
        return SceneLocalLightTuning{.radius_scale = 1.0f, .intensity_scale = 0.95f};
    }
    return SceneLocalLightTuning{.radius_scale = 0.92f, .intensity_scale = 0.72f};
}

glm::vec3 placeable_table_light_color(int32_t color) noexcept
{
    if (color < 0) {
        return glm::vec3{0.0f};
    }

    return tile_main_light_debug_color(static_cast<uint8_t>(std::clamp(color, 0, 31)));
}

SceneLocalLightTuning scene_light_tuning_for_source(
    const PreviewScene& scene,
    SceneLocalLightSource source) noexcept
{
    switch (source) {
    case SceneLocalLightSource::authored_model:
        return scene_authored_model_light_tuning(scene);
    case SceneLocalLightSource::tile_model:
        return scene_local_light_tuning(scene);
    case SceneLocalLightSource::placeable_table:
        return scene_placeable_table_light_tuning(scene);
    }
    return scene_authored_model_light_tuning(scene);
}

glm::vec3 render_model_light_color(
    const nw::render::ModelLight& light,
    const SceneTileLightSlots& slots)
{
    glm::vec3 color = light.color;
    if (has_tile_light_slots(slots)
        && light.external_color_slot != nw::render::kModelLightNoExternalColor
        && light.external_color_slot < 4u) {
        const std::array<uint8_t, 4> color_indices{
            slots.main1,
            slots.main2,
            slots.source1,
            slots.source2,
        };
        color = tile_color_from_index(color_indices[light.external_color_slot]);
    }
    return glm::clamp(color, glm::vec3{0.0f}, glm::vec3{1.0f});
}

SceneLocalLight scene_local_light_from_render_model_light(
    const nw::render::ModelLight& light,
    const glm::mat4& node_world_transform,
    const SceneModelLightAppendOptions& options)
{
    const bool has_authored_tile_slots = has_tile_light_slots(options.tile_slots);
    auto world_position = glm::vec3(node_world_transform[3]);
    if (has_authored_tile_slots) {
        const float tile_base_z = glm::vec3(options.root_transform[3]).z;
        const float z_offset = light.main_contribution ? 1.6f : 1.1f;
        world_position.z = std::min(world_position.z, tile_base_z + z_offset);
    }

    const float minimum_radius = has_authored_tile_slots
        ? (light.main_contribution ? 6.0f : 3.4f)
        : 2.6f;
    const float maximum_radius = has_authored_tile_slots
        ? (light.main_contribution ? 9.2f : 5.4f)
        : 32.0f;
    const float minimum_intensity = has_authored_tile_slots
        ? (light.main_contribution ? 0.30f : 0.46f)
        : 0.05f;
    const float maximum_intensity = has_authored_tile_slots
        ? (light.main_contribution ? 1.0f : 1.35f)
        : 2.0f;
    const float contribution_scale = has_authored_tile_slots && light.main_contribution ? 0.45f : 1.0f;
    const float base_radius = std::clamp(
        light.radius > 0.0f ? light.radius : minimum_radius,
        minimum_radius,
        maximum_radius);
    const float base_intensity = std::clamp(
                                     std::max(light.intensity, minimum_intensity),
                                     0.0f,
                                     maximum_intensity)
        * contribution_scale;
    const bool ambient_contribution = light.ambient
        || (has_authored_tile_slots && light.main_contribution);

    return SceneLocalLight{
        .position = world_position,
        .radius = base_radius * options.tuning.radius_scale,
        .color = render_model_light_color(light, options.tile_slots),
        .intensity = base_intensity * options.tuning.intensity_scale,
        .base_radius = base_radius,
        .base_intensity = base_intensity,
        .source = options.source,
        .model_index = options.model_index,
        .model_source_node_index = light.node <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            ? static_cast<int32_t>(light.node)
            : -1,
        .tile_light_slots = options.tile_slots,
        .tile_x = static_cast<uint16_t>(
            options.tile_x >= 0
                ? std::clamp(options.tile_x, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))
                : 0),
        .tile_y = static_cast<uint16_t>(
            options.tile_y >= 0
                ? std::clamp(options.tile_y, 0, static_cast<int>(std::numeric_limits<uint16_t>::max()))
                : 0),
        .tile_orientation = options.tile_orientation,
        .dynamic = static_cast<uint8_t>(light.dynamic ? 1 : 0),
        .affect_dynamic = static_cast<uint8_t>(light.affect_dynamic ? 1 : 0),
        .ambient_contribution = static_cast<uint8_t>(ambient_contribution ? 1 : 0),
        .casts_shadow = static_cast<uint8_t>(light.casts_shadow ? 1 : 0),
        .fading = static_cast<uint8_t>(light.fading ? 1 : 0),
    };
}

nw::render::LocalLight render_local_light_from_scene_light(const SceneLocalLight& light) noexcept
{
    return nw::render::LocalLight{
        .position = light.position,
        .radius = light.radius,
        .color = light.color,
        .intensity = light.intensity,
        .contribution = light.ambient_contribution != 0
            ? nw::render::LocalLightContribution::ambient
            : nw::render::LocalLightContribution::diffuse,
        .vertical_scale = light.ambient_contribution != 0 ? 0.45f : 1.0f,
        .casts_shadow = light.casts_shadow != 0 && light.ambient_contribution == 0,
    };
}

void apply_scene_light_tuning(PreviewScene& scene, SceneLocalLight& light) noexcept
{
    if (light.base_radius <= 0.0f) {
        light.base_radius = light.radius;
    }
    if (light.base_intensity <= 0.0f) {
        light.base_intensity = light.intensity;
    }

    const SceneLocalLightTuning tuning = scene_light_tuning_for_source(scene, light.source);
    light.radius = light.base_radius * tuning.radius_scale;
    light.intensity = light.base_intensity * tuning.intensity_scale;
}

void append_scene_local_light(PreviewScene& scene, SceneLocalLight light)
{
    apply_scene_light_tuning(scene, light);
    scene.render_local_lights.push_back(render_local_light_from_scene_light(light));
    scene.local_lights.push_back(light);
    if (scene.is_area && viewer_tile_light_debug_shapes_enabled()) {
        LOG_F(INFO,
            "local_light source={} pos=[{:.2f},{:.2f},{:.2f}] radius={:.2f} intensity={:.2f} color=[{:.2f},{:.2f},{:.2f}] ambient={} shadow={}",
            static_cast<int>(light.source),
            light.position.x,
            light.position.y,
            light.position.z,
            light.radius,
            light.intensity,
            light.color.r,
            light.color.g,
            light.color.b,
            light.ambient_contribution,
            light.casts_shadow);
    }
}

void append_debug_light_marker(PreviewScene& scene, const SceneLocalLight& light)
{
    constexpr float k_marker_z_offset = 0.1f;
    constexpr float k_marker_width = 0.055f;

    const glm::vec3 center = light.position + glm::vec3{0.0f, 0.0f, k_marker_z_offset};
    const glm::vec3 rgb = glm::clamp(light.color * std::clamp(light.intensity, 0.35f, 1.4f),
        glm::vec3{0.0f},
        glm::vec3{1.0f});
    const glm::vec4 color{rgb, 0.86f};

    append_debug_segment(
        scene,
        center + glm::vec3{-0.35f, 0.0f, 0.0f},
        center + glm::vec3{0.35f, 0.0f, 0.0f},
        color,
        k_marker_width);
    append_debug_segment(
        scene,
        center + glm::vec3{0.0f, -0.35f, 0.0f},
        center + glm::vec3{0.0f, 0.35f, 0.0f},
        color,
        k_marker_width);
}

bool scene_light_tracks_model_node(const SceneLocalLight& light) noexcept
{
    return light.source == SceneLocalLightSource::authored_model
        && light.model_index != kInvalidSceneLocalLightModelIndex
        && light.model_source_node_index >= 0;
}

bool disable_scene_render_light(PreviewScene& scene, size_t light_index) noexcept
{
    if (light_index >= scene.render_local_lights.size()) {
        return false;
    }

    auto& light = scene.render_local_lights[light_index];
    const bool changed = light.radius != 0.0f
        || light.intensity != 0.0f
        || light.casts_shadow
        || light.shadow_slot != -1;
    light.radius = 0.0f;
    light.intensity = 0.0f;
    light.casts_shadow = false;
    light.shadow_slot = -1;
    return changed;
}

bool scene_light_model_node_position(
    const PreviewScene& scene,
    const SceneLocalLight& light,
    glm::vec3& out_position) noexcept
{
    if (light.model_source_node_index < 0) {
        return false;
    }

    if (light.model_index >= scene.static_models.size()) {
        return false;
    }
    const auto* instance = scene.static_model_instance(light.model_index);
    glm::mat4 node_world{1.0f};
    if (!instance
        || !instance->visible
        || !nw::render::model_instance_node_world_transform(
            *instance, light.model_source_node_index, node_world)) {
        return false;
    }
    out_position = glm::vec3(node_world[3]);
    return std::isfinite(out_position.x)
        && std::isfinite(out_position.y)
        && std::isfinite(out_position.z);
}

size_t append_render_model_light_rows(
    PreviewScene& scene,
    const nw::render::RenderModel& model,
    const nw::render::ModelInstance& instance,
    SceneModelLightAppendOptions options)
{
    options.root_transform = instance.root_transform;
    size_t result = 0;
    for (const auto& light : model.lights) {
        if (light.node >= model.nodes.size()) {
            continue;
        }

        glm::mat4 node_world = instance.root_transform * model.nodes[light.node].world_transform;
        if (light.node <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            glm::mat4 sampled_node_world{1.0f};
            if (nw::render::model_instance_node_world_transform(
                    instance, static_cast<int32_t>(light.node), sampled_node_world)) {
                node_world = sampled_node_world;
            }
        }
        const auto scene_light = scene_local_light_from_render_model_light(light, node_world, options);
        append_scene_local_light(scene, scene_light);
        if (viewer_tile_light_debug_shapes_enabled()) {
            const size_t first_debug_index = scene.debug_shape_indices.size();
            append_debug_light_marker(scene, scene_light);
            append_debug_shape_range(scene, DebugShapeCategory::general, first_debug_index);
        }
        ++result;
    }
    return result;
}

} // namespace

SceneLocalLightTuning scene_local_light_tuning(const PreviewScene& scene) noexcept
{
    const bool interior = (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool underground = (scene.area_flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    const bool night = scene.area_weather.is_night != 0 || underground;

    if (interior) {
        return SceneLocalLightTuning{.radius_scale = 0.82f, .intensity_scale = 0.52f};
    }
    if (underground) {
        return SceneLocalLightTuning{.radius_scale = 0.82f, .intensity_scale = 0.52f};
    }
    if (night) {
        return SceneLocalLightTuning{.radius_scale = 0.80f, .intensity_scale = 0.50f};
    }
    return SceneLocalLightTuning{.radius_scale = 0.62f, .intensity_scale = 0.28f};
}

SceneTileLightSlots scene_tile_light_slots(const nw::AreaTile& tile) noexcept
{
    return SceneTileLightSlots{
        .main1 = tile.mainlight1,
        .main2 = tile.mainlight2,
        .source1 = tile.srclight1,
        .source2 = tile.srclight2,
    };
}

size_t append_placeable_table_light(
    PreviewScene& scene,
    const nw::Location& location,
    const nw::ObjectVisualLight& lighting)
{
    constexpr float k_base_radius = 6.0f;
    constexpr float k_base_intensity = 0.86f;

    const glm::vec3 color = placeable_table_light_color(lighting.light_color);
    if (!has_visible_light_color(color)) {
        if (viewer_tile_light_debug_shapes_enabled()) {
            LOG_F(INFO,
                "placeable_table_light skipped color=[{:.2f},{:.2f},{:.2f}] light_color={}",
                color.r,
                color.g,
                color.b,
                lighting.light_color);
        }
        return 0;
    }

    const SceneLocalLightTuning tuning = scene_placeable_table_light_tuning(scene);
    const glm::mat4 placement = area_object_placement_transform(location);
    const glm::vec3 world_position = glm::vec3(placement * glm::vec4{lighting.light_offset, 1.0f});

    SceneLocalLight light{
        .position = world_position,
        .radius = k_base_radius * tuning.radius_scale,
        .color = color,
        .intensity = k_base_intensity * tuning.intensity_scale,
        .base_radius = k_base_radius,
        .base_intensity = k_base_intensity,
        .source = SceneLocalLightSource::placeable_table,
        .dynamic = 0,
        .affect_dynamic = 1,
    };
    append_scene_local_light(scene, light);
    if (viewer_tile_light_debug_shapes_enabled()) {
        const size_t first_debug_index = scene.debug_shape_indices.size();
        append_debug_light_marker(scene, light);
        append_debug_shape_range(scene, DebugShapeCategory::general, first_debug_index);
    }
    return 1;
}

size_t append_placeable_table_lights(
    PreviewScene& scene,
    const nw::Location& location,
    const nw::ObjectVisualState* visual)
{
    if (!visual) { return 0; }

    size_t result = 0;
    for (const auto& light : visual->lights) {
        result += append_placeable_table_light(scene, location, light);
    }
    return result;
}

size_t append_render_model_authored_lights(PreviewScene& scene, size_t model_index)
{
    if (model_index >= scene.static_models.size()
        || model_index > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return 0;
    }

    const auto& model = scene.static_models[model_index];
    const auto* instance = scene.static_model_instance(model_index);
    if (!model || !instance || !instance->visible) {
        return 0;
    }

    return append_render_model_light_rows(scene, *model, *instance, SceneModelLightAppendOptions{
                                                                        .source = SceneLocalLightSource::authored_model,
                                                                        .model_index = static_cast<uint32_t>(model_index),
                                                                        .tuning = scene_authored_model_light_tuning(scene),
                                                                    });
}

size_t append_scene_authored_model_lights(PreviewScene& scene)
{
    size_t result = 0;
    for (size_t model_index = 0; model_index < scene.static_models.size(); ++model_index) {
        result += append_render_model_authored_lights(scene, model_index);
    }
    return result;
}

size_t append_tile_render_model_lights(
    PreviewScene& scene,
    size_t model_index,
    const nw::AreaTile& tile,
    int tile_x,
    int tile_y)
{
    if (model_index >= scene.static_models.size()
        || model_index > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return 0;
    }
    const auto& model = scene.static_models[model_index];
    const auto* instance = scene.static_model_instance(model_index);
    if (!model || !instance || !instance->visible) {
        return 0;
    }

    const SceneTileLightSlots slots = scene_tile_light_slots(tile);
    return append_render_model_light_rows(scene, *model, *instance, SceneModelLightAppendOptions{
                                                                        .tile_slots = slots,
                                                                        .tile_x = tile_x,
                                                                        .tile_y = tile_y,
                                                                        .tile_orientation = static_cast<uint8_t>(std::clamp(tile.orientation, 0, 255)),
                                                                        .source = SceneLocalLightSource::tile_model,
                                                                        .model_index = static_cast<uint32_t>(model_index),
                                                                        .tuning = scene_local_light_tuning(scene),
                                                                    });
}

void refresh_scene_local_light_render_data(PreviewScene& scene)
{
    scene.render_local_lights.clear();
    scene.render_local_lights.reserve(scene.local_lights.size());
    for (auto& light : scene.local_lights) {
        apply_scene_light_tuning(scene, light);
        scene.render_local_lights.push_back(render_local_light_from_scene_light(light));
    }
}

bool refresh_scene_dynamic_local_light_render_data(PreviewScene& scene)
{
    bool changed = false;
    if (scene.render_local_lights.size() != scene.local_lights.size()) {
        refresh_scene_local_light_render_data(scene);
        changed = true;
    }

    for (size_t i = 0; i < scene.local_lights.size(); ++i) {
        auto& light = scene.local_lights[i];
        glm::vec3 position{0.0f};
        if (!scene_light_model_node_position(scene, light, position)) {
            if (scene_light_tracks_model_node(light) && disable_scene_render_light(scene, i)) {
                changed = true;
            }
            continue;
        }

        const glm::vec3 delta = position - light.position;
        const bool moved = glm::dot(delta, delta) > 1.0e-8f;
        const bool disabled = i < scene.render_local_lights.size()
            && (scene.render_local_lights[i].radius == 0.0f || scene.render_local_lights[i].intensity == 0.0f);
        if (!moved && !disabled) {
            continue;
        }

        if (moved) {
            light.position = position;
        }
        if (i < scene.render_local_lights.size()) {
            scene.render_local_lights[i] = render_local_light_from_scene_light(light);
        }
        changed = true;
    }
    return changed;
}

} // namespace nw::render::viewer
