#include "tile_light.hpp"

#include <nw/kernel/TwoDACache.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace nw::render::viewer {

namespace {

float tile_color_max_channel(const glm::vec3& color) noexcept
{
    return std::max(color.x, std::max(color.y, color.z));
}

glm::vec3 tile_light_hue(uint8_t index) noexcept
{
    static constexpr std::array<glm::vec3, 7> colors{
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

float light_authored_radius(const nw::model::LightNode& light) noexcept
{
    const auto radius_ctrl = light.get_controller(nw::model::ControllerType::Radius);
    if (radius_ctrl.key && !radius_ctrl.data.empty()) {
        return radius_ctrl.data[0];
    }
    const auto shadow_ctrl = light.get_controller(nw::model::ControllerType::ShadowRadius);
    if (shadow_ctrl.key && !shadow_ctrl.data.empty()) {
        return shadow_ctrl.data[0];
    }
    return light.flareradius;
}

} // namespace

bool has_tile_light_slots(const SceneTileLightSlots& slots) noexcept
{
    return slots.main1 != 0 || slots.main2 != 0 || slots.source1 != 0 || slots.source2 != 0;
}

glm::vec3 tile_color_from_index(uint8_t index)
{
    const auto* tda = nw::kernel::twodas().get("tilecolor");
    if (!tda || tda->rows() == 0) {
        return glm::vec3{0.0f};
    }
    const size_t row = std::min<size_t>(index, tda->rows() - 1);
    glm::vec3 color{
        tda->get<float>(row, "Red").value_or(0.0f),
        tda->get<float>(row, "Green").value_or(0.0f),
        tda->get<float>(row, "Blue").value_or(0.0f),
    };
    // Original NWN data stores channels as 0-255 integers; some custom/modern
    // variants may already be normalized. Normalize when we see out-of-gamut
    // values so both layouts produce consistent linear colours.
    if (tile_color_max_channel(color) > 1.0f) {
        color /= 255.0f;
    }
    return glm::clamp(color, glm::vec3{0.0f}, glm::vec3{1.0f});
}

glm::vec3 tile_main_light_color(uint8_t value) noexcept
{
    static constexpr std::array<float, 4> white_strengths{
        0.0f, 0.45f, 0.76f, 1.0f};
    static constexpr std::array<float, 4> hue_strengths{
        0.36f, 0.56f, 0.78f, 1.0f};
    if (value <= 3) {
        return glm::vec3{white_strengths[value]};
    }
    if (value > 31) {
        return glm::vec3{0.0f};
    }

    const uint8_t hue_index = static_cast<uint8_t>((value - 4) / 4);
    const uint8_t strength_index = static_cast<uint8_t>((value - 4) % 4);
    return tile_light_hue(hue_index) * hue_strengths[strength_index];
}

glm::vec3 tile_source_light_color(uint8_t value) noexcept
{
    if (value > 15) {
        return glm::vec3{0.0f};
    }
    const uint8_t main_value = value == 0 ? 0
        : value == 1                      ? 2
                                          : static_cast<uint8_t>(value * 2);
    return tile_main_light_color(main_value);
}

TileLightSlot parse_tile_light_slot(StringView name) noexcept
{
    const size_t n = name.size();
    if (n < 3) {
        return {};
    }
    const char digit = name[n - 1];
    if (digit != '1' && digit != '2') {
        return {};
    }
    const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(name[n - 3])));
    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(name[n - 2])));
    if (b != 'l' || (a != 'm' && a != 's')) {
        return {};
    }
    return TileLightSlot{true, a == 'm', digit == '2'};
}

uint8_t tile_slot_color_index(const SceneTileLightSlots& slots, const TileLightSlot& slot) noexcept
{
    if (slot.is_main) {
        return slot.second ? slots.main2 : slots.main1;
    }
    return slot.second ? slots.source2 : slots.source1;
}

bool model_light_is_main_tile_slot(const nw::model::LightNode& light) noexcept
{
    const auto slot = parse_tile_light_slot(light.name);
    if (slot.valid) {
        return slot.is_main;
    }
    return light_authored_radius(light) >= 8.0f;
}

glm::vec3 tile_slot_color_for_model_light(
    const nw::model::LightNode& light,
    const SceneTileLightSlots& slots) noexcept
{
    const auto slot = parse_tile_light_slot(light.name);
    if (!slot.valid) {
        return glm::vec3{0.0f};
    }
    const uint8_t value = tile_slot_color_index(slots, slot);
    return slot.is_main
        ? tile_main_light_color(value)
        : tile_source_light_color(value);
}

} // namespace nw::render::viewer
