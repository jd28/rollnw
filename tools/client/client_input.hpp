#pragma once

#include "client_input_routes.hpp"

#include <RmlUi/Core/Types.h>
#include <SDL3/SDL.h>

#include <optional>
#include <string_view>

namespace Rml {
class Context;
class ElementDocument;
class Element;
}

namespace nw::toolset {

// DOM/focus borrows last only for the call. Hidden ancestors exclude targets.
// These query one current UI context; they do not cache facts across callbacks.
[[nodiscard]] bool point_within_element(
    Rml::ElementDocument* doc, std::string_view id, Rml::Vector2f point);
[[nodiscard]] bool focused_text_input(Rml::Context* context);
[[nodiscard]] bool focused_element_has_id(Rml::Context* context, const char* id);

Rml::Vector2f to_context_point(SDL_Window* window, float x, float y);
Rml::Element* element_at_mouse(Rml::Context* context, SDL_Window* window,
    const SDL_MouseButtonEvent& event);
// End an edit before native mouse handling consumes a click outside its input.
// Blur may replace markup; reacquire any subsequent DOM target after this call.
void blur_focused_object_variable_input(Rml::Context* context, Rml::Vector2f point);

// Current mode/capture facts supplied by their actual owners. This flat input
// owns no feature state. Visibility and focus are captured again by the adapter.
struct ClientInputOwnership {
    ClientInputMap map = ClientInputMap::invalid;
    ClientPointerOwner pointer_owner = ClientPointerOwner::none;
    bool world_available = false;
    bool command_modal = false;
    bool world_input_blocked = false;
    bool lifecycle_key = false;
    bool release_before_native = false;
};

ClientInputFacts capture_client_input_facts(const SDL_Event&, SDL_Window*,
    Rml::Context* toolset, Rml::Context* command, Rml::ElementDocument* palette,
    Rml::ElementDocument* modals, ClientInputOwnership ownership);
// Current ordered event is a singleton wrapper over the flat batch route.
// Capture after earlier callbacks; no event/DOM borrow or cached facts escape.
ClientInputRoute resolve_client_event_input_route(const SDL_Event&, SDL_Window*,
    Rml::Context* toolset, Rml::Context* command, Rml::ElementDocument* palette,
    Rml::ElementDocument* modals, ClientInputOwnership ownership);
// Eligibility for already-claimed pending pointer input and current held device
// state. There is one displayed session; callers resolve this as count = 1.
ClientInputFacts capture_client_held_input_facts(Rml::Context* toolset,
    Rml::Context* command, Rml::ElementDocument* palette,
    Rml::ElementDocument* modals, ClientInputOwnership ownership);

// Default forwarding after native/exclusive UI handling: captures only its
// required palette facts, then resolves count = 1. Native fields are not used.
ClientInputRoute resolve_client_forward_route(const SDL_Event&, SDL_Window*,
    Rml::ElementDocument* palette, ClientInputMap map);

// One borrowed ordered event, with no retained event payload or DOM target.
// Native handling is independent of the SDK's propagation return value.
struct ClientInputDispatchState {
    bool native_handled = false;
    ClientRmlRecipient forwarded_recipient = ClientRmlRecipient::none;
    ClientRmlForwardPhase forwarding_phase = ClientRmlForwardPhase::none;
};
struct ClientInputForwardResult {
    bool performed = false;
    bool propagating = false;
};

bool client_input_forwarding_pending(const ClientInputDispatchState& dispatch) noexcept;
// Finite, representable mouse coordinates and finite wheel values; normalized
// touch coordinates remain in [0,1]. Invalid data is rejected before DOM queries.
bool valid_client_pointer_event(const SDL_Event& event) noexcept;
// Rejected pointer input cancels the UI press/touch without clicking its old
// hover target. Clears SDK mouse capture; no SDL event is forwarded.
void cancel_client_pointer_interactions(Rml::Context* toolset, Rml::Context* command,
    const SDL_Event& event);
// Records the obligation before SDK callbacks; at most one call per event.
// Explicit after-native left UI release may finish a consumed native click;
// native consumption still suppresses default forwarding and all other events.
// Null context/unknown tags/unsafe scaled motion reject without forwarding.
ClientInputForwardResult forward_client_input(ClientInputDispatchState& dispatch,
    ClientRmlRecipient recipient, ClientRmlForwardPhase phase,
    Rml::Context* context, SDL_Window* window, SDL_Event& event);

// The native virtual row's data-key and the SDK release share one ordered event.
// Query copies and strictly parses the complete int32 key; no DOM borrow escapes.
// Negative values are parsed; each feature enforces its own valid index range.
std::optional<int32_t> client_row_key(Rml::Element* row);
// Parses before SDK dispatch; malformed keys still release the SDK press.
// The caller validates the returned index against its current feature owner.
std::optional<int32_t> release_client_row_key(ClientInputDispatchState& dispatch,
    Rml::Element* row, Rml::Context* context, SDL_Window* window, SDL_Event& event);

} // namespace nw::toolset
