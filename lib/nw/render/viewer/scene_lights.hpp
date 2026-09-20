#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>

namespace nw {
struct AreaTile;
struct Location;
struct ObjectVisualLight;
struct ObjectVisualState;
}

namespace nw::model {
class Mdl;
}

namespace nw::render::viewer {

struct PreviewScene;
struct SceneTileLightSlots;

struct SceneLocalLightTuning {
    float radius_scale = 1.0f;
    float intensity_scale = 1.0f;
};

enum class SceneTileLightRefreshStatus : uint8_t {
    invalid,
    stable_rows,
    reindexed_rows,
};

SceneLocalLightTuning scene_local_light_tuning(const PreviewScene& scene) noexcept;
[[nodiscard]] bool scene_light_debug_markers_enabled() noexcept;
SceneTileLightSlots scene_tile_light_slots(const nw::AreaTile& tile) noexcept;
size_t append_placeable_table_light(
    PreviewScene& scene,
    const nw::Location& location,
    const nw::ObjectVisualLight& lighting);
size_t append_placeable_table_lights(
    PreviewScene& scene,
    const nw::Location& location,
    const nw::ObjectVisualState* visual);
size_t append_render_model_authored_lights(PreviewScene& scene, size_t model_index);
size_t append_scene_authored_model_lights(PreviewScene& scene);
size_t append_tile_render_model_lights(
    PreviewScene& scene, size_t model_index, const nw::AreaTile& tile, int tile_x, int tile_y);
// Replaces a sorted batch of tile-model light rows. When every tile retains
// its prior light count, local/render rows are overwritten in place and the
// sorted changed indices describe the stable rows. A count change compacts
// the affected rows and reports reindexed_rows so render caches can rebuild.
SceneTileLightRefreshStatus refresh_scene_tile_model_lights(
    PreviewScene& scene,
    std::span<const uint32_t> model_indices,
    std::span<const nw::AreaTile> tiles,
    std::vector<uint32_t>& changed_light_indices);
void refresh_scene_local_light_render_data(PreviewScene& scene);
bool refresh_scene_dynamic_local_light_render_data(PreviewScene& scene);
bool refresh_scene_dynamic_local_light_render_data(
    PreviewScene& scene, std::span<const uint32_t> model_indices);

} // namespace nw::render::viewer
