#include "runtime_input.hpp"

#include <SDL3/SDL.h>
#include <nw/log.hpp>

#include <algorithm>
#include <cmath>

namespace nw::toolset {
namespace {

float normalized_gamepad_axis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) noexcept
{
    const Sint16 value = SDL_GetGamepadAxis(gamepad, axis);
    return value < 0 ? static_cast<float>(value) / 32768.0f
                     : static_cast<float>(value) / 32767.0f;
}

} // namespace

void RuntimeGamepadDeleter::operator()(SDL_Gamepad* gamepad) const noexcept
{
    SDL_CloseGamepad(gamepad);
}

void open_runtime_gamepad(RuntimeInputState& input, uint32_t id)
{
    if (input.gamepad || id == 0) { return; }
    input.gamepad.reset(SDL_OpenGamepad(id));
    if (!input.gamepad) {
        LOG_F(WARNING, "Play preview: failed to open gamepad: {}", SDL_GetError());
    }
}

void close_runtime_gamepad(RuntimeInputState& input) noexcept
{
    input.gamepad.reset();
}

void reset_runtime_pending_input(RuntimeInputState& input) noexcept
{
    input.pending = {};
    input.mouse_look_pixels = {};
    input.mouse_sample_seconds = 0.0;
    input.wheel_zoom = 0.0f;
}

void consume_runtime_input_edges(RuntimeInputState& input) noexcept
{
    input.pending.flags &= ~(preview_input_click_target | preview_input_cancel | preview_input_click_door);
    input.mouse_look_pixels = {};
    input.mouse_sample_seconds = 0.0;
    input.wheel_zoom = 0.0f;
}

PreviewStatus acquire_pc_device_sample(RuntimeInputState& input, double frame_seconds,
    PcSourceEligibility eligibility, PcDeviceSample& sample) noexcept
{
    sample = {};
    if (!std::isfinite(frame_seconds)) { return PreviewStatus::invalid_input; }
    if (!eligibility.pointer) {
        clear_preview_pointer_action(input.pending);
        input.mouse_look_pixels = {};
        input.mouse_sample_seconds = 0.0;
        input.wheel_zoom = 0.0f;
    } else {
        input.mouse_sample_seconds += std::max(0.0, frame_seconds);
    }
    if (!eligibility.controller) { input.pending.flags &= ~preview_input_cancel; }
    sample.pending = input.pending;
    sample.mouse_look_pixels = input.mouse_look_pixels;
    sample.mouse_sample_seconds = input.mouse_sample_seconds;
    sample.wheel_zoom = input.wheel_zoom;
    sample.eligibility = eligibility;
    // This list describes physical acquisition only; bindings live in pc_input.
    constexpr std::array scancodes{SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_Q,
        SDL_SCANCODE_E, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_LEFT,
        SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN};
    static_assert(scancodes.size() == static_cast<size_t>(PcKey::count));
    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);
    if (keys) {
        for (size_t index = 0; index < scancodes.size(); ++index) {
            if (scancodes[index] < key_count) { sample.keys[index] = keys[scancodes[index]]; }
        }
    }
    auto* gamepad = input.gamepad.get();
    sample.controller_connected = gamepad && SDL_GamepadConnected(gamepad);
    if (sample.controller_connected) {
        sample.left_stick = {normalized_gamepad_axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX), normalized_gamepad_axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)};
        sample.right_stick = {normalized_gamepad_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX), normalized_gamepad_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY)};
        sample.left_trigger = normalized_gamepad_axis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        sample.right_trigger = normalized_gamepad_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        sample.left_shoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        sample.right_shoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    }
    return PreviewStatus::ok;
}

} // namespace nw::toolset
