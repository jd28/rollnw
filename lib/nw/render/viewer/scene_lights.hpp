#pragma once

#include <cstddef>

#include <glm/mat4x4.hpp>

namespace nw {
struct AreaTile;
struct Placeable;
struct PlaceableInfo;
}

namespace nw::model {
struct Mdl;
}

namespace nw::render::viewer {

struct PreviewScene;
struct SceneTileLightSlots;

struct SceneLocalLightTuning {
    float radius_scale = 1.0f;
    float intensity_scale = 1.0f;
};

SceneLocalLightTuning scene_local_light_tuning(const PreviewScene& scene) noexcept;
SceneTileLightSlots scene_tile_light_slots(const nw::AreaTile& tile) noexcept;
size_t append_placeable_table_light(PreviewScene& scene, const nw::Placeable& placeable, const nw::PlaceableInfo& info);
size_t append_model_authored_lights(PreviewScene& scene, size_t model_index);
size_t append_scene_authored_model_lights(PreviewScene& scene);
size_t append_tile_model_lights(
    PreviewScene& scene, const nw::model::Mdl& mdl, const glm::mat4& tile_placement, const nw::AreaTile& tile, int tile_x, int tile_y);
void refresh_scene_local_light_render_data(PreviewScene& scene);
bool refresh_scene_dynamic_local_light_render_data(PreviewScene& scene);

} // namespace nw::render::viewer
