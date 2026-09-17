#pragma once
#include "client_application_state.hpp"
#include "renderer.hpp"
#include <RmlUi_Platform_SDL.h>

#include "client_ui_action.hpp"

namespace nw::toolset::client_application_detail {

// Private composition functions. Feature APIs receive only their own states.
// Cold SDK borrows for one ordered main-thread application. Owners stay in
// desktop startup; callbacks may mutate the documents synchronously. Dimensions
// are the existing current logical/pixel sizes, not a second viewport cache.
struct ClientApplicationSurfaces {
    SDL_Window* window;
    SystemInterface_SDL* system_interface;
    Rml::Context* context;
    Rml::Context* fps_context;
    Rml::Context* palette_context;
    Rml::ElementDocument* doc;
    Rml::ElementDocument* fps_doc;
    Rml::ElementDocument* palette_doc;
    int width;
    int height;
    int frame_width;
    int frame_height;
};

// finish retains the existing final forwarding obligation; next skips it.
inline constexpr float kManagedListAutoScrollEdgePx = 28.0f;
inline constexpr float kManagedListAutoScrollStepPx = 14.0f;
inline constexpr float kWorkspaceTabDragThresholdPx = 5.0f;
inline constexpr float kTabScrollStepPx = 48.0f;

enum class ClientEventFlow { finish,
    next };

class ObjectWorkbenchChangeListener;

nw::toolset::ClientInputOwnership client_input_ownership(const ClientApplicationState& state);

nw::toolset::ClientInputRoute client_world_pointer_route(const SDL_Event& event, SDL_Window* window,
    Rml::Context* context, Rml::Context* palette_context, Rml::ElementDocument* palette_doc,
    const ClientApplicationState& state, WorkspaceViewerViewportKind viewport);

nw::toolset::ClientUiActionOwner client_ui_action_owner(const ClientApplicationState& state);

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id);

Rml::Element* find_ancestor_with_class(Rml::Element* element, std::string_view class_name);

Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id);

bool point_within_viewport(ClientViewportRect rect, Rml::Vector2f point);

bool shift_only(SDL_Keymod modifiers) noexcept;

nw::toolset::AreaTilePointerModifier area_tile_pointer_modifier(
    SDL_Keymod modifiers) noexcept;

nw::toolset::AreaTilePointerButton area_tile_pointer_button(
    uint8_t button) noexcept;

bool command_palette_contains_point(
    Rml::ElementDocument* palette_doc, const ClientApplicationState& state, Rml::Vector2f point);

void clear_rml_focus(Rml::Context* context);

void focus_workspace_viewport(Rml::ElementDocument* doc, ClientApplicationState& state);

bool viewport_mouse_hit_blocked(Rml::ElementDocument* doc, Rml::Element* top_hit, Rml::Vector2f point, const ClientApplicationState& state);

bool recent_list_hit_blocked(Rml::ElementDocument* doc, Rml::Element* top_hit, Rml::Vector2f point, const ClientApplicationState& state);

std::string get_input_value(Rml::ElementDocument* doc, const char* id);

void set_input_value(Rml::ElementDocument* doc, const char* id, std::string_view value);

void poll_client_input(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state, ObjectWorkbenchChangeListener& object_workbench_change_listener, bool& running);

} // namespace nw::toolset::client_application_detail
