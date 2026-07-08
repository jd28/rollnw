#include "area_lighting.hpp"

#include "scene_lights.hpp"

#include <nw/log.hpp>
#include <nw/objects/Area.hpp>

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace nw::render::viewer {
namespace {

static constexpr float kAreaViewerTwilightHoldStrength = 0.72f;

glm::vec3 srgb8_to_linear(uint32_t packed)
{
    const auto decode = [](uint8_t channel) {
        return std::pow(static_cast<float>(channel) / 255.0f, 2.2f);
    };
    return glm::vec3{
        decode(static_cast<uint8_t>(packed & 0xffu)),
        decode(static_cast<uint8_t>((packed >> 8u) & 0xffu)),
        decode(static_cast<uint8_t>((packed >> 16u) & 0xffu)),
    };
}

glm::vec3 authored_light_color(uint32_t packed, const glm::vec3& fallback)
{
    if ((packed & 0x00ffffffu) == 0u) {
        return fallback;
    }

    const auto decoded = srgb8_to_linear(packed);
    if (std::max(decoded.x, std::max(decoded.y, decoded.z)) <= 1.0e-4f) {
        return fallback;
    }
    return decoded;
}

float saturate(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float smooth_blend(float edge0, float edge1, float value)
{
    if (edge0 == edge1) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 mix_vec3(const glm::vec3& a, const glm::vec3& b, float t)
{
    return a + (b - a) * t;
}

glm::vec2 direction_from_angle(float angle) noexcept
{
    return glm::vec2{std::cos(angle), std::sin(angle)};
}

glm::vec3 celestial_direction(float azimuth, float height)
{
    const glm::vec3 dir{
        std::cos(azimuth),
        std::sin(azimuth),
        std::max(height, 0.08f),
    };
    return glm::normalize(dir);
}

float area_cycle_time(const PreviewScene& scene, float elapsed_seconds)
{
    if (!scene.is_area || scene.area_weather.day_night_cycle == 0) {
        return scene.area_weather.is_night ? 0.75f : 0.25f;
    }
    return normalize_area_day_night_elapsed_seconds(elapsed_seconds) / kAreaDayNightCycleSeconds;
}

float twilight_weighted_cycle_time(float cycle_t)
{
    return cycle_t - (kAreaViewerTwilightHoldStrength / (4.0f * glm::pi<float>())) * std::sin(cycle_t * 4.0f * glm::pi<float>());
}

struct AreaCycleWeights {
    float cycle_t = 0.25f;
    float orbit_angle = glm::half_pi<float>();
    float sun_height = 1.0f;
    float sun_weight = 1.0f;
    float moon_weight = 0.0f;
    float azimuth = 0.0f;
    float dusk_strength = 0.0f;
};

AreaCycleWeights resolve_area_cycle_weights(const PreviewScene& scene, float elapsed_seconds)
{
    AreaCycleWeights result{};
    result.cycle_t = twilight_weighted_cycle_time(area_cycle_time(scene, elapsed_seconds));
    result.orbit_angle = result.cycle_t * glm::two_pi<float>();
    result.sun_height = std::sin(result.orbit_angle);
    result.sun_weight = smooth_blend(-0.38f, 0.34f, result.sun_height);
    result.moon_weight = 1.0f - result.sun_weight;
    result.azimuth = result.orbit_angle - glm::half_pi<float>();
    const float twilight = 1.0f - std::abs(result.sun_weight * 2.0f - 1.0f);
    result.dusk_strength = twilight * twilight;
    return result;
}

Lighting unlit_preview_lighting()
{
    Lighting result{};
    result.key_intensity = 0.0f;
    result.fill_intensity = 0.0f;
    result.rim_intensity = 0.0f;
    result.ambient = glm::vec3{0.0f};
    return result;
}

Lighting gltf_preview_lighting()
{
    return unlit_preview_lighting();
}

Lighting area_inspection_daylight_lighting(const PreviewScene& scene)
{
    constexpr float key_azimuth = -0.65f;
    constexpr float key_height = 0.72f;

    Lighting result{};
    result.key_direction = celestial_direction(key_azimuth, key_height);
    result.key_color = authored_light_color(scene.area_weather.color_sun_diffuse, glm::vec3{1.0f, 0.96f, 0.86f});
    result.key_intensity = 1.10f;
    result.fill_direction = celestial_direction(key_azimuth + 1.8f, 0.22f);
    result.fill_color = glm::vec3{0.42f, 0.48f, 0.58f};
    result.fill_intensity = 0.22f;
    result.rim_direction = celestial_direction(key_azimuth + glm::pi<float>(), 0.16f);
    result.rim_color = result.key_color;
    result.rim_intensity = 0.08f;
    result.ambient = authored_light_color(scene.area_weather.color_sun_ambient, glm::vec3{0.25f, 0.26f, 0.27f})
        * 0.38f;
    return result;
}

Lighting area_authored_celestial_lighting(const PreviewScene& scene, float elapsed_seconds)
{
    const bool interior = (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool underground = (scene.area_flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    const bool camera_relative = interior || underground;

    float night_weight = 0.0f;
    float key_azimuth = -0.65f;
    float key_height = 0.72f;
    if (scene.area_weather.day_night_cycle != 0) {
        const auto cycle = resolve_area_cycle_weights(scene, elapsed_seconds);
        night_weight = cycle.moon_weight;
        key_azimuth = cycle.azimuth;
        key_height = std::max(cycle.sun_height, 0.16f);
    } else {
        night_weight = scene.area_weather.is_night != 0 ? 1.0f : 0.0f;
        key_height = night_weight > 0.5f ? 0.45f : 0.72f;
    }

    const glm::vec3 sun_key_color = authored_light_color(
        scene.area_weather.color_sun_diffuse, glm::vec3{1.0f, 0.96f, 0.86f});
    const glm::vec3 moon_key_color = authored_light_color(
        scene.area_weather.color_moon_diffuse, glm::vec3{0.78f, 0.82f, 1.0f});
    const glm::vec3 sun_ambient = authored_light_color(
        scene.area_weather.color_sun_ambient, glm::vec3{0.25f, 0.26f, 0.27f});
    const glm::vec3 moon_ambient = authored_light_color(
        scene.area_weather.color_moon_ambient, glm::vec3{0.10f, 0.12f, 0.16f});

    Lighting result{};
    if (!camera_relative) {
        result.key_direction = celestial_direction(key_azimuth, key_height);
        result.fill_direction = celestial_direction(key_azimuth + 1.8f, 0.18f);
        result.rim_direction = celestial_direction(key_azimuth + glm::pi<float>(), 0.12f);
    }

    result.key_color = mix_vec3(sun_key_color, moon_key_color, night_weight);
    result.key_intensity = glm::mix(1.00f, 0.80f, night_weight);
    result.fill_color = mix_vec3(glm::vec3{0.34f, 0.39f, 0.48f},
        glm::vec3{0.20f, 0.24f, 0.34f}, night_weight);
    result.fill_intensity = glm::mix(0.14f, 0.055f, night_weight);
    result.rim_color = result.key_color;
    result.rim_intensity = glm::mix(0.05f, 0.030f, night_weight);

    const float ambient_scale = glm::mix(0.75f, 1.10f, night_weight);
    result.ambient = mix_vec3(sun_ambient, moon_ambient, night_weight) * ambient_scale;
    result.ambient = glm::max(result.ambient, result.key_color * result.key_intensity * 0.04f);
    return result;
}

} // namespace

bool supports_area_day_night_cycle(const PreviewScene& scene) noexcept
{
    return scene.is_area
        && (scene.area_flags & nw::AreaFlags::interior) == nw::AreaFlags::none
        && scene.area_weather.day_night_cycle != 0;
}

float normalize_area_day_night_elapsed_seconds(float elapsed_seconds) noexcept
{
    float result = std::fmod(elapsed_seconds, kAreaDayNightCycleSeconds);
    if (result < 0.0f) {
        result += kAreaDayNightCycleSeconds;
    }
    return result;
}

float initial_area_day_night_elapsed_seconds(const PreviewScene& scene) noexcept
{
    return scene.area_weather.is_night
        ? kAreaDayNightCycleSeconds * 0.75f
        : kAreaDayNightCycleSeconds * 0.25f;
}

bool sync_area_day_night_state(PreviewScene& scene, float elapsed_seconds, bool log_transition)
{
    if (!supports_area_day_night_cycle(scene)) {
        return false;
    }

    const float cycle_t = twilight_weighted_cycle_time(area_cycle_time(scene, elapsed_seconds));
    const bool is_night = std::sin(cycle_t * glm::two_pi<float>()) < 0.0f;
    if (scene.area_weather.is_night != static_cast<uint8_t>(is_night)) {
        scene.area_weather.is_night = static_cast<uint8_t>(is_night);
        refresh_scene_local_light_render_data(scene);
        if (log_transition) {
            LOG_F(INFO, "Area viewer lighting crossed into {}", is_night ? "night" : "day");
        }
        return true;
    }
    return false;
}

Lighting resolve_preview_scene_lighting(
    const PreviewScene& scene,
    float area_day_night_elapsed_seconds,
    AreaLightingMode mode)
{
    if (!scene.static_models.empty()) {
        return gltf_preview_lighting();
    }

    if (!scene.is_area) {
        return studio_preview_lighting();
    }

    if (mode == AreaLightingMode::inspection_daylight) {
        return area_inspection_daylight_lighting(scene);
    }

    return area_authored_celestial_lighting(scene, area_day_night_elapsed_seconds);
}

LightingSpace resolve_preview_scene_lighting_space(const PreviewScene& scene, AreaLightingMode mode) noexcept
{
    if (mode == AreaLightingMode::inspection_daylight && scene.is_area) {
        return LightingSpace::world_space;
    }
    if (scene.is_area && (scene.area_flags & nw::AreaFlags::interior) == nw::AreaFlags::none) {
        return LightingSpace::world_space;
    }
    return LightingSpace::camera_relative;
}

SceneFog resolve_preview_scene_fog(
    const PreviewScene& scene, float area_day_night_elapsed_seconds, bool authored_area_fog_enabled)
{
    if (!authored_area_fog_enabled) {
        return {};
    }

    if (!scene.is_area || (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none) {
        return {};
    }

    const auto& weather = scene.area_weather;
    const bool has_sun_color = (weather.color_sun_fog & 0x00ffffffu) != 0u;
    const bool has_moon_color = (weather.color_moon_fog & 0x00ffffffu) != 0u;
    if ((!has_sun_color && !has_moon_color) || weather.fog_clip_distance <= 0.0f) {
        return {};
    }

    const auto cycle = resolve_area_cycle_weights(scene, area_day_night_elapsed_seconds);
    const glm::vec3 sun_color = has_sun_color
        ? srgb8_to_linear(weather.color_sun_fog)
        : srgb8_to_linear(weather.color_moon_fog);
    const glm::vec3 moon_color = has_moon_color
        ? srgb8_to_linear(weather.color_moon_fog)
        : srgb8_to_linear(weather.color_sun_fog);

    const float moon_amount = std::clamp(static_cast<float>(weather.fog_moon_amount) / 15.0f, 0.0f, 1.0f);
    const float sun_amount = std::clamp(static_cast<float>(weather.fog_sun_amount) / 15.0f, 0.0f, 1.0f);
    const float authored_amount = moon_amount + (sun_amount - moon_amount) * cycle.sun_weight;
    const float preview_amount = authored_amount > 0.0f ? authored_amount : 0.18f;

    SceneFog result{};
    result.enabled = true;
    result.color = mix_vec3(moon_color, sun_color, cycle.sun_weight);
    result.amount = preview_amount;
    result.end_distance = weather.fog_clip_distance;
    result.start_distance = result.end_distance * glm::mix(0.76f, 0.50f, preview_amount);
    return result;
}

SceneEnvironment resolve_preview_scene_environment(
    const PreviewScene& scene, float area_day_night_elapsed_seconds) noexcept
{
    SceneEnvironment result{};
    if (!scene.is_area) {
        return result;
    }

    const auto& weather = scene.area_weather;
    const bool interior = (scene.area_flags & nw::AreaFlags::interior) != nw::AreaFlags::none;
    const bool underground = (scene.area_flags & nw::AreaFlags::underground) != nw::AreaFlags::none;
    const float shelter_scale = interior ? 0.18f : underground ? 0.42f
                                                               : 1.0f;

    const int32_t rain_chance = std::clamp(weather.chance_rain, int32_t{0}, int32_t{100});
    const int32_t snow_chance = std::clamp(weather.chance_snow, int32_t{0}, int32_t{100});
    const int32_t dominant_weather = std::max(rain_chance, snow_chance);
    const bool snow = snow_chance > rain_chance;
    result.weather_density = shelter_scale * (static_cast<float>(dominant_weather) / 100.0f);
    result.weather_type = dominant_weather == 0
        ? SceneWeatherType::clear
        : snow ? SceneWeatherType::snow
               : SceneWeatherType::rain;
    result.area_flags = static_cast<uint32_t>(scene.area_flags);

    const float authored_wind = std::clamp(static_cast<float>(weather.wind_power) / 10.0f, 0.0f, 1.0f);
    result.wind_strength = std::clamp((0.05f + authored_wind * 0.82f + result.weather_density * 0.24f) * shelter_scale,
        0.025f, 1.0f);
    result.wind_speed = 0.72f + result.wind_strength * 1.45f;

    const float seed = static_cast<float>(
        scene.area_width * 37 + scene.area_height * 17 + weather.wind_power * 11 + dominant_weather * 3);
    const float base_angle = 0.73f + std::fmod(seed * 0.61803398875f, glm::two_pi<float>());
    const float cycle_t = normalize_area_day_night_elapsed_seconds(area_day_night_elapsed_seconds)
        / kAreaDayNightCycleSeconds;
    const float slow_drift = std::sin(cycle_t * glm::two_pi<float>()) * (0.05f + authored_wind * 0.10f);
    result.wind_direction = direction_from_angle(base_angle + slow_drift);

    static const float water_quality = [] {
        const char* env = std::getenv("ROLLNW_VIEWER_WATER_QUALITY");
        if (!env) {
            return 1.0f;
        }
        const float value = std::strtof(env, nullptr);
        return std::clamp(value, 0.0f, 3.0f);
    }();
    result.water_quality = water_quality;

    return result;
}

} // namespace nw::render::viewer
