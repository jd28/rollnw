#pragma once

#include "preview_scene.hpp"

namespace nw::render::viewer {

inline constexpr float kAreaDayNightCycleSeconds = 45.0f;

enum class AreaLightingMode {
    authored,
    inspection_daylight,
};

[[nodiscard]] bool supports_area_day_night_cycle(const PreviewScene& scene) noexcept;
[[nodiscard]] float normalize_area_day_night_elapsed_seconds(float elapsed_seconds) noexcept;
[[nodiscard]] float initial_area_day_night_elapsed_seconds(const PreviewScene& scene) noexcept;
bool sync_area_day_night_state(PreviewScene& scene, float elapsed_seconds, bool log_transition = false);

[[nodiscard]] Lighting resolve_preview_scene_lighting(
    const PreviewScene& scene,
    float area_day_night_elapsed_seconds,
    AreaLightingMode mode = AreaLightingMode::authored);
[[nodiscard]] LightingSpace resolve_preview_scene_lighting_space(
    const PreviewScene& scene,
    AreaLightingMode mode = AreaLightingMode::authored) noexcept;
[[nodiscard]] SceneFog resolve_preview_scene_fog(
    const PreviewScene& scene, float area_day_night_elapsed_seconds, bool authored_area_fog_enabled);
[[nodiscard]] SceneEnvironment resolve_preview_scene_environment(
    const PreviewScene& scene, float area_day_night_elapsed_seconds) noexcept;

} // namespace nw::render::viewer
