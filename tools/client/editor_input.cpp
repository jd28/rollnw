#include "editor_input.hpp"

#include <nw/objects/ObjectHandle.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace nw::toolset {
namespace {

EditorShortcutAction shortcut_action(const EditorShortcutInput& input) noexcept
{
    const auto modifiers = input.modifiers;
    switch (input.key) {
    case SDLK_P:
        return (modifiers & SDL_KMOD_CTRL) && (modifiers & SDL_KMOD_SHIFT)
            ? EditorShortcutAction::palette_toggle
            : EditorShortcutAction::none;
    case SDLK_J:
        return modifiers & SDL_KMOD_CTRL ? EditorShortcutAction::output_toggle : EditorShortcutAction::none;
    case SDLK_GRAVE:
        return !(modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))
            ? EditorShortcutAction::terminal_toggle
            : EditorShortcutAction::none;
    case SDLK_S:
        if (!input.repeat && (modifiers & SDL_KMOD_CTRL)
            && !(modifiers & (SDL_KMOD_ALT | SDL_KMOD_GUI))) {
            return modifiers & SDL_KMOD_SHIFT ? EditorShortcutAction::save_all : EditorShortcutAction::save_tab;
        }
        break;
    case SDLK_W:
    case SDLK_Z:
    case SDLK_Y:
        if (!input.repeat && (modifiers & SDL_KMOD_CTRL)
            && !(modifiers & (SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
            if (input.key == SDLK_W) { return EditorShortcutAction::close_tab; }
            return input.key == SDLK_Z ? EditorShortcutAction::undo : EditorShortcutAction::redo;
        }
        break;
    default:
        break;
    }
    return EditorShortcutAction::none;
}

std::optional<ClientViewportCameraCommand> camera_binding(SDL_Keycode key, bool preview)
{
    switch (key) {
    case SDLK_W:
        return preview ? ClientViewportCameraCommand::pitch_up : ClientViewportCameraCommand::move_forward;
    case SDLK_S:
        return preview ? ClientViewportCameraCommand::pitch_down : ClientViewportCameraCommand::move_backward;
    case SDLK_A:
        return preview ? ClientViewportCameraCommand::yaw_left : ClientViewportCameraCommand::move_left;
    case SDLK_D:
        return preview ? ClientViewportCameraCommand::yaw_right : ClientViewportCameraCommand::move_right;
    case SDLK_Q:
        return preview ? ClientViewportCameraCommand::zoom_out : ClientViewportCameraCommand::move_down;
    case SDLK_E:
        return preview ? ClientViewportCameraCommand::zoom_in : ClientViewportCameraCommand::move_up;
    case SDLK_LEFT:
        return ClientViewportCameraCommand::yaw_left;
    case SDLK_RIGHT:
        return ClientViewportCameraCommand::yaw_right;
    case SDLK_UP:
        return ClientViewportCameraCommand::pitch_up;
    case SDLK_DOWN:
        return ClientViewportCameraCommand::pitch_down;
    case SDLK_F:
        return ClientViewportCameraCommand::fit;
    case SDLK_G:
        return preview ? std::nullopt : std::optional{ClientViewportCameraCommand::gameplay};
    default:
        return std::nullopt;
    }
}

EditorKeyAction resolve(const EditorKeyInput& input)
{
    EditorKeyAction action;
    auto key_facts = input.facts;
    key_facts.pointer_owner = ClientPointerOwner::none;
    std::array<ClientInputRoute, 1> routes{};
    if (input.facts.category != ClientInputCategory::key || input.facts.edge != ClientInputEdge::down
        || input.viewport > EditorViewportKind::preview
        || input.facts.pointer_owner > ClientPointerOwner::pc
        || !resolve_client_input_routes({&key_facts, 1}, routes)
        || routes.front().native != ClientNativeRecipient::editor || input.facts.world_input_blocked
        || (input.modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) { return action; }
    if (!input.repeat && !(input.modifiers & SDL_KMOD_SHIFT)) {
        if (input.key == SDLK_R && input.tile_action_allowed) {
            action.kind = EditorKeyActionKind::rotate_tiles;
            return action;
        }
        if (input.viewport == EditorViewportKind::area) {
            if (input.key == SDLK_DELETE) {
                action.kind = EditorKeyActionKind::remove_object;
                return action;
            }
            if (input.key == SDLK_R) {
                action.kind = EditorKeyActionKind::randomize_object_orientation;
                return action;
            }
        }
    }
    const auto command = camera_binding(input.key, input.viewport == EditorViewportKind::preview);
    if (!command) { return action; }
    if (input.viewport == EditorViewportKind::none) {
        action.clear_viewport_focus = true;
        return action;
    }
    if (!input.viewport_focused && !(input.tiles_active && input.viewport == EditorViewportKind::area)) { return action; }
    action.kind = EditorKeyActionKind::camera;
    action.camera = *command;
    action.scale = input.modifiers & SDL_KMOD_SHIFT ? 3.0f : 1.0f;
    return action;
}

} // namespace

bool resolve_editor_shortcut_actions(std::span<const EditorShortcutInput> inputs,
    std::span<EditorShortcutAction> outputs) noexcept
{
    if (inputs.size() != outputs.size()) {
        std::ranges::fill(outputs, EditorShortcutAction::none);
        return false;
    }
    for (size_t i = 0; i < inputs.size(); ++i) {
        outputs[i] = shortcut_action(inputs[i]);
    }
    return true;
}

bool editor_key_has_binding(SDL_Keycode key) noexcept
{
    return key == SDLK_R || key == SDLK_DELETE || camera_binding(key, false).has_value();
}

bool resolve_editor_key_actions(std::span<const EditorKeyInput> inputs,
    std::span<EditorKeyAction> outputs) noexcept
{
    if (inputs.size() != outputs.size()) {
        std::ranges::fill(outputs, EditorKeyAction{});
        return false;
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        outputs[index] = resolve(inputs[index]);
    }
    return true;
}

bool resolve_editor_wheel_actions(std::span<const EditorWheelInput> inputs,
    std::span<EditorWheelAction> outputs) noexcept
{
    if (inputs.size() != outputs.size()) {
        std::ranges::fill(outputs, EditorWheelAction{});
        return false;
    }
    for (size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        auto& action = outputs[index];
        action = {};
        if (input.recipient != ClientNativeRecipient::editor || input.viewport == EditorViewportKind::none
            || input.viewport > EditorViewportKind::preview || !std::isfinite(input.amount) || input.amount == 0) { continue; }
        action = {EditorWheelActionKind::camera_zoom, input.amount};
        if (input.viewport != EditorViewportKind::area || input.text_focused) { continue; }
        if (input.object_type == ObjectType::sound
            && !(input.modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))) {
            action.kind = EditorWheelActionKind::sound_radius;
        } else if ((input.object_type == ObjectType::creature || input.object_type == ObjectType::item || input.object_type == ObjectType::placeable)
            && !(input.modifiers & (SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_SHIFT))) {
            action.kind = input.modifiers & SDL_KMOD_CTRL ? EditorWheelActionKind::object_rotate : EditorWheelActionKind::object_scale;
        }
    }
    return true;
}

} // namespace nw::toolset
