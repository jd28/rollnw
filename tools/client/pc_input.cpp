#include "pc_input.hpp"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>

namespace nw::toolset {
namespace {

bool finite(glm::vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(glm::vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool normalized_axis(float value) noexcept
{
    return std::isfinite(value) && value >= -1.0f && value <= 1.0f;
}

bool valid_devices(const PcDeviceSample& input) noexcept
{
    return finite(input.mouse_look_pixels)
        && std::isfinite(input.mouse_sample_seconds) && input.mouse_sample_seconds >= 0.0
        && std::isfinite(input.wheel_zoom)
        && (!input.controller_connected
            || (normalized_axis(input.left_stick.x) && normalized_axis(input.left_stick.y)
                && normalized_axis(input.right_stick.x) && normalized_axis(input.right_stick.y)
                && normalized_axis(input.left_trigger) && normalized_axis(input.right_trigger)));
}

bool valid_pointer(const PreviewInputSample& input) noexcept
{
    constexpr auto pointer_flags = preview_input_click_target | preview_input_click_door;
    return (input.flags & ~(pointer_flags | preview_input_cancel)) == 0
        && (input.flags & pointer_flags) != pointer_flags
        && (!(input.flags & preview_input_click_target) || finite(input.click_target))
        && (!(input.flags & preview_input_click_door)
            || (input.door_index != UINT32_MAX
                && finite(input.door_bounds_min) && finite(input.door_bounds_max)
                && !glm::any(glm::greaterThan(input.door_bounds_min, input.door_bounds_max))));
}

} // namespace

PreviewStatus translate_pc_input_samples(std::span<const PcDeviceSample> inputs,
    std::span<PreviewInputSample> outputs) noexcept
{
    const auto reject = [&outputs]() {
        std::fill(outputs.begin(), outputs.end(), PreviewInputSample{});
        return PreviewStatus::invalid_input;
    };
    if (inputs.size() != outputs.size()) { return reject(); }
    for (size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        if (!valid_devices(input)) { return reject(); }
        auto sample = input.pending;
        if (!input.eligibility.pointer) { clear_preview_pointer_action(sample); }
        if (!input.eligibility.controller) { sample.flags &= ~preview_input_cancel; }
        if (!valid_pointer(sample)) { return reject(); }
        const auto key = [&input](PcKey value) {
            return input.eligibility.keyboard
                ? static_cast<float>(input.keys[static_cast<size_t>(value)])
                : 0.0f;
        };
        glm::vec2 keyboard{key(PcKey::e) - key(PcKey::q), key(PcKey::w) - key(PcKey::s)};
        sample.turn_axis = key(PcKey::d) - key(PcKey::a);
        const glm::vec2 keyboard_look{key(PcKey::right) - key(PcKey::left), key(PcKey::down) - key(PcKey::up)};
        glm::vec2 gamepad_move{};
        glm::vec2 gamepad_look{};
        float gamepad_zoom = 0.0f;
        if (input.controller_connected && input.eligibility.controller) {
            gamepad_move = preview_radial_deadzone({input.left_stick.x, -input.left_stick.y});
            gamepad_look = preview_radial_deadzone(input.right_stick);
            gamepad_zoom = input.right_trigger - input.left_trigger;
            gamepad_zoom += static_cast<float>(input.right_shoulder) - static_cast<float>(input.left_shoulder);
        }
        sample.move_axis = glm::clamp(keyboard + gamepad_move, glm::vec2{-1.0f}, glm::vec2{1.0f});
        sample.look_axis = keyboard_look + gamepad_look;
        constexpr float mouse_radians_per_pixel = 0.0035f;
        constexpr float look_radians_per_second = 2.5f;
        if (input.eligibility.pointer && input.mouse_sample_seconds > 0.0) {
            const float mouse_scale = mouse_radians_per_pixel
                / (look_radians_per_second * static_cast<float>(input.mouse_sample_seconds));
            sample.look_axis += input.mouse_look_pixels * mouse_scale;
        }
        sample.zoom_axis = gamepad_zoom;
        sample.zoom_delta = input.eligibility.pointer ? input.wheel_zoom : 0.0f;
        if (!finite(sample.look_axis) || !std::isfinite(sample.zoom_axis)) { return reject(); }
        outputs[index] = sample;
    }
    return PreviewStatus::ok;
}

PreviewStatus apply_pc_pointer_actions(std::span<const PcPointerAction> inputs,
    std::span<PreviewInputSample> pending) noexcept
{
    const auto reject = [&pending]() {
        for (auto& sample : pending) {
            clear_preview_pointer_action(sample);
        }
        return PreviewStatus::invalid_input;
    };
    if (inputs.size() != pending.size()) { return reject(); }
    for (const auto& input : inputs) {
        if (input.door_interactable) {
            if (input.door_index == UINT32_MAX || !finite(input.door_bounds_min)
                || !finite(input.door_bounds_max)
                || glm::any(glm::greaterThan(input.door_bounds_min, input.door_bounds_max))) {
                return reject();
            }
        } else if (input.navigation_projected && !finite(input.navigation_position)) {
            return reject();
        }
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        auto& sample = pending[index];
        clear_preview_pointer_action(sample);
        if (input.door_interactable) {
            (void)set_preview_click_door(sample, input.door_index, input.door_bounds_min, input.door_bounds_max);
        } else if (input.navigation_projected) {
            (void)set_preview_click_target(sample, input.navigation_position);
        }
    }
    return PreviewStatus::ok;
}

PreviewStatus apply_pc_controller_button_edges(std::span<const PcControllerButtonEdge> inputs,
    std::span<PreviewInputSample> pending) noexcept
{
    if (inputs.size() != pending.size()
        || std::ranges::any_of(inputs, [](const auto& edge) { return edge.button > PcControllerButton::back; })) {
        for (auto& sample : pending) {
            sample.flags &= ~preview_input_cancel;
        }
        return PreviewStatus::invalid_input;
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        const auto& edge = inputs[index];
        if (edge.enabled && edge.down && (edge.button == PcControllerButton::east || edge.button == PcControllerButton::back)) {
            pending[index].flags |= preview_input_cancel;
        }
    }
    return PreviewStatus::ok;
}

} // namespace nw::toolset
