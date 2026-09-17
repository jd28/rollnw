#pragma once

#include "client_input_routes.hpp"
#include "viewport_rect.hpp"

#include <SDL3/SDL.h>

#include <span>

namespace nw {
enum class ObjectType : uint32_t;
}

namespace nw::toolset {

enum class EditorShortcutAction : uint8_t { none,
    palette_toggle,
    output_toggle,
    terminal_toggle,
    close_tab,
    save_tab,
    save_all,
    undo,
    redo };
// Existing editor application shortcuts, before world-map bindings. Independent
// key-down rows own immutable SDK values; no focus/mode facts are cached here.
// Caller applies each tag at its original priority; this API selects no PC map.
struct EditorShortcutInput {
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod modifiers = SDL_KMOD_NONE;
    bool repeat = false;
};
// Unknown keys produce none; ignored SDK modifier bits retain current policy.
// Mismatched spans clear all outputs and reject; empty batches succeed.
bool resolve_editor_shortcut_actions(std::span<const EditorShortcutInput>,
    std::span<EditorShortcutAction>) noexcept;

enum class EditorViewportKind : uint8_t { none,
    area,
    preview };
enum class EditorKeyActionKind : uint8_t { none,
    rotate_tiles,
    remove_object,
    randomize_object_orientation,
    camera };

// Schema revision 1. Independently captured key-down rows contain no event,
// SDK or engine borrow. Equal-length contiguous batches are caller-owned for
// this call. Root resolves count=1 after earlier callbacks have completed.
struct EditorKeyInput {
    ClientInputFacts facts;
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod modifiers = SDL_KMOD_NONE;
    EditorViewportKind viewport = EditorViewportKind::none;
    bool repeat = false;
    bool viewport_focused = false;
    bool tiles_active = false;
    bool tile_action_allowed = false;
};
struct EditorKeyAction {
    EditorKeyActionKind kind = EditorKeyActionKind::none;
    ClientViewportCameraCommand camera = ClientViewportCameraCommand::fit;
    float scale = 1;
    bool clear_viewport_focus = false;
};

// Pointer capture does not claim keyboard input; earlier exclusive gesture
// handling stays with its owner. Unbound keys need no viewport/focus capture.
bool editor_key_has_binding(SDL_Keycode key) noexcept;
// Only the editor native recipient may produce actions. UI ownership, blocked
// world input, PC/unknown maps and malformed tags reject. Missing viewport may
// clear camera focus, never select another map. Unequal spans clear all outputs.
bool resolve_editor_key_actions(std::span<const EditorKeyInput> inputs,
    std::span<EditorKeyAction> outputs) noexcept;

enum class EditorWheelActionKind : uint8_t { none,
    camera_zoom,
    sound_radius,
    object_scale,
    object_rotate };
// Schema 1. Consume the current native recipient from the routing authority;
// no independent map selection. Caller owns equal-span flat batches for the
// call. Other object types retain camera zoom, including unknown type values.
struct EditorWheelInput {
    ClientNativeRecipient recipient = ClientNativeRecipient::none;
    EditorViewportKind viewport = EditorViewportKind::none;
    nw::ObjectType object_type{};
    SDL_Keymod modifiers = SDL_KMOD_NONE;
    float amount = 0;
    bool text_focused = false;
};
struct EditorWheelAction {
    EditorWheelActionKind kind = EditorWheelActionKind::none;
    float amount = 0;
};
// Unknown/missing viewport, non-editor recipient and zero/nonfinite amounts
// reject per row; mismatched spans clear all output and return false.
bool resolve_editor_wheel_actions(std::span<const EditorWheelInput> inputs,
    std::span<EditorWheelAction> outputs) noexcept;

} // namespace nw::toolset
