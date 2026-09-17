#include "client_input.hpp"
#include <RmlUi_Platform_SDL.h>
#include <optional>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace nw::toolset {

bool point_within_element(Rml::ElementDocument* doc, std::string_view id, Rml::Vector2f point)
{
    auto* element = doc ? doc->GetElementById(std::string(id)) : nullptr;
    return element && element->IsVisible(true)
        && element->IsPointWithinElement(point);
}

bool focused_text_input(Rml::Context* context)
{
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus) {
        return false;
    }

    if (!focus->IsVisible(true)) {
        return false;
    }

    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        const Rml::String id = cursor->GetId();
        if (id == "command_input"
            || id == "terminal_input"
            || id == "recent_search"
            || id == "output_filter") {
            return true;
        }
        if (cursor->GetTagName() == "input"
            || cursor->GetTagName() == "textarea") {
            return true;
        }
    }
    return false;
}

bool focused_element_has_id(Rml::Context* context, const char* id)
{
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus || !id || !focus->IsVisible(true)) {
        return false;
    }

    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->GetId() == id) {
            return true;
        }
    }
    return false;
}

Rml::Vector2f to_context_point(SDL_Window* window, float x, float y)
{
    (void)window;
    return Rml::Vector2f{x, y};
}

namespace {

bool coordinate(float value) noexcept
{
    return std::isfinite(value)
        && static_cast<double>(value) >= static_cast<double>(std::numeric_limits<int>::min())
        && static_cast<double>(value) <= static_cast<double>(std::numeric_limits<int>::max());
}

Rml::Element* ancestor_with_class(Rml::Element* element, const char* name)
{
    for (; element; element = element->GetParentNode()) {
        if (element->IsClassSet(name)) { return element; }
    }
    return nullptr;
}

} // namespace

Rml::Element* element_at_mouse(Rml::Context* context, SDL_Window* window, const SDL_MouseButtonEvent& mouse)
{
    if (!coordinate(mouse.x) || !coordinate(mouse.y)) { return nullptr; }
    if (!context || !window) {
        return nullptr;
    }

    return context->GetElementAtPoint(Rml::Vector2f{static_cast<float>(mouse.x), static_cast<float>(mouse.y)});
}

void blur_focused_object_variable_input(
    Rml::Context* context, Rml::Vector2f point)
{
    if (!coordinate(point.x) || !coordinate(point.y)) { return; }
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus
        || (!focus->IsClassSet("object_variable_name")
            && !focus->IsClassSet("object_variable_value"))) {
        return;
    }

    auto* hit = context->GetElementAtPoint(point);
    auto* hit_input = ancestor_with_class(hit, "object_variable_name");
    if (!hit_input) {
        hit_input = ancestor_with_class(hit, "object_variable_value");
    }
    if (hit_input != focus) {
        focus->Blur();
    }
}

bool client_input_forwarding_pending(const ClientInputDispatchState& dispatch) noexcept
{
    return !dispatch.native_handled && dispatch.forwarded_recipient == ClientRmlRecipient::none;
}

bool valid_client_pointer_event(const SDL_Event& event) noexcept
{
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        return coordinate(event.motion.x) && coordinate(event.motion.y)
            && std::isfinite(event.motion.xrel) && std::isfinite(event.motion.yrel);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return coordinate(event.button.x) && coordinate(event.button.y);
    case SDL_EVENT_MOUSE_WHEEL:
        return coordinate(event.wheel.mouse_x) && coordinate(event.wheel.mouse_y)
            && std::isfinite(event.wheel.x) && std::isfinite(event.wheel.y);
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
        return std::isfinite(event.tfinger.x) && std::isfinite(event.tfinger.y)
            && event.tfinger.x >= 0 && event.tfinger.x <= 1
            && event.tfinger.y >= 0 && event.tfinger.y <= 1;
    default:
        return true;
    }
}

void cancel_client_pointer_interactions(Rml::Context* toolset, Rml::Context* command,
    const SDL_Event& event)
{
    SDL_CaptureMouse(false);
    for (auto* context : {toolset, command}) {
        if (!context) { continue; }
        if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_UP
            || event.type == SDL_EVENT_FINGER_MOTION) {
            const Rml::TouchList touches{{.identifier = static_cast<Rml::TouchId>(event.tfinger.fingerID), .position = {0, 0}}};
            context->ProcessTouchCancel(touches);
        } else {
            context->ProcessMouseLeave();
            context->ProcessMouseButtonUp(0, RmlSDL::GetKeyModifierState());
        }
    }
}

ClientInputForwardResult forward_client_input(ClientInputDispatchState& dispatch,
    ClientRmlRecipient recipient, ClientRmlForwardPhase phase,
    Rml::Context* context, SDL_Window* window, SDL_Event& event)
{
    const bool completed_native_ui_release = phase == ClientRmlForwardPhase::after_native
        && event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT;
    if ((dispatch.native_handled && !completed_native_ui_release) || dispatch.forwarded_recipient != ClientRmlRecipient::none || !context
        || (recipient != ClientRmlRecipient::toolset && recipient != ClientRmlRecipient::command)
        || (phase != ClientRmlForwardPhase::before_native && phase != ClientRmlForwardPhase::after_native)
        || !valid_client_pointer_event(event)) { return {}; }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (!window) { return {}; }
        const float density = SDL_GetWindowPixelDensity(window);
        if (!std::isfinite(density) || density <= 0
            || !coordinate(event.motion.x * density) || !coordinate(event.motion.y * density)) { return {}; }
    }
    dispatch.forwarded_recipient = recipient;
    dispatch.forwarding_phase = phase;
    return {.performed = true, .propagating = RmlSDL::InputEventHandler(context, window, event)};
}

std::optional<int32_t> client_row_key(Rml::Element* row)
{
    if (!row) { return std::nullopt; }
    const auto key = row->GetAttribute<Rml::String>("data-key", "");
    int32_t result = 0;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), result);
    return !key.empty() && parsed.ec == std::errc{} && parsed.ptr == key.data() + key.size()
        ? std::optional<int32_t>{result}
        : std::nullopt;
}

std::optional<int32_t> release_client_row_key(ClientInputDispatchState& dispatch,
    Rml::Element* row, Rml::Context* context, SDL_Window* window, SDL_Event& event)
{
    if (!row || !context || !client_input_forwarding_pending(dispatch)
        || event.type != SDL_EVENT_MOUSE_BUTTON_UP
        || event.button.button != SDL_BUTTON_LEFT || !valid_client_pointer_event(event)) {
        return std::nullopt;
    }
    const auto key = client_row_key(row);
    // Dispatch may remove row and replace its markup. Only copied values survive.
    const auto released = forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::before_native, context, window, event);
    return released.performed ? key : std::nullopt;
}

namespace {
bool visible_input_element(Rml::ElementDocument* document, const char* id)
{
    auto* element = document ? document->GetElementById(id) : nullptr;
    return element && element->IsVisible(true);
}
ClientInputFacts capture_ui_facts(Rml::Context* toolset, Rml::Context* command,
    Rml::ElementDocument* modals, bool palette_visible, ClientInputOwnership ownership)
{
    ClientInputFacts facts;
    facts.map = ownership.map;
    facts.pointer_owner = ownership.pointer_owner;
    facts.world_available = ownership.world_available;
    facts.command_modal = ownership.command_modal || visible_input_element(modals, "command_form_overlay");
    facts.world_input_blocked = ownership.world_input_blocked || facts.command_modal
        || palette_visible;
    facts.lifecycle_key = ownership.lifecycle_key;
    facts.release_before_native = ownership.release_before_native;
    facts.focus = focused_text_input(command) ? ClientInputFocus::command_text
        : focused_text_input(toolset)         ? ClientInputFocus::toolset_text
                                              : ClientInputFocus::none;
    return facts;
}
} // namespace

ClientInputFacts capture_client_held_input_facts(Rml::Context* toolset,
    Rml::Context* command, Rml::ElementDocument* palette,
    Rml::ElementDocument* modals, ClientInputOwnership ownership)
{
    const bool palette_visible = visible_input_element(palette, "command_palette");
    auto facts = capture_ui_facts(toolset, command, modals, palette_visible, ownership);
    facts.category = ClientInputCategory::held;
    facts.target = ClientInputTarget::world;
    return facts;
}

ClientInputFacts capture_client_input_facts(const SDL_Event& event, SDL_Window* window,
    Rml::Context* toolset, Rml::Context* command, Rml::ElementDocument* palette,
    Rml::ElementDocument* modals, ClientInputOwnership ownership)
{
    // No focus/hit/coordinate query occurs for a rejected raw pointer event.
    if (!valid_client_pointer_event(event)) {
        ClientInputFacts invalid;
        invalid.pointer_valid = false;
        return invalid;
    }
    const bool palette_visible = visible_input_element(palette, "command_palette");
    auto facts = capture_ui_facts(toolset, command, modals, palette_visible, ownership);
    std::optional<Rml::Vector2f> point;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        facts.category = ClientInputCategory::key;
        facts.edge = event.type == SDL_EVENT_KEY_DOWN ? ClientInputEdge::down : ClientInputEdge::up;
        break;
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
        facts.category = ClientInputCategory::text;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        facts.category = ClientInputCategory::pointer;
        facts.edge = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? ClientInputEdge::down : ClientInputEdge::up;
        point = to_context_point(window, event.button.x, event.button.y);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        facts.category = ClientInputCategory::pointer;
        facts.edge = ClientInputEdge::motion;
        point = to_context_point(window, event.motion.x, event.motion.y);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        facts.category = ClientInputCategory::pointer;
        facts.edge = ClientInputEdge::wheel;
        point = to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        facts.category = ClientInputCategory::gamepad;
        facts.edge = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? ClientInputEdge::down : ClientInputEdge::up;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        facts.category = ClientInputCategory::gamepad;
        break;
    default:
        break;
    }
    if (point) {
        if (palette_visible && point_within_element(palette, "command_palette", *point)) {
            facts.target = ClientInputTarget::command;
        } else {
            auto* hit = toolset ? toolset->GetElementAtPoint(*point) : nullptr;
            bool viewport = false;
            for (auto* element = hit; element; element = element->GetParentNode()) {
                if (element->GetId() == "workspace_viewer_viewport") {
                    viewport = true;
                    break;
                }
            }
            facts.target = viewport || !hit ? ClientInputTarget::world : ClientInputTarget::toolset;
        }
    } else if (palette_visible && (facts.category == ClientInputCategory::key || facts.category == ClientInputCategory::text || event.type == SDL_EVENT_TEXT_EDITING_CANDIDATES)) {
        facts.target = ClientInputTarget::command;
    }
    return facts;
}

ClientInputRoute resolve_client_event_input_route(const SDL_Event& event, SDL_Window* window,
    Rml::Context* toolset, Rml::Context* command, Rml::ElementDocument* palette,
    Rml::ElementDocument* modals, ClientInputOwnership ownership)
{
    const std::array facts{capture_client_input_facts(event, window, toolset, command, palette, modals, ownership)};
    std::array<ClientInputRoute, 1> routes{};
    (void)resolve_client_input_routes(facts, routes);
    return routes[0];
}

ClientInputRoute resolve_client_forward_route(const SDL_Event& event, SDL_Window* window,
    Rml::ElementDocument* palette, ClientInputMap map)
{
    // Native/exclusive UI handling has already run. Recipient selection needs
    // only visible palette bounds, avoiding another toolset hit/focus traversal.
    return resolve_client_event_input_route(event, window, nullptr, nullptr, palette, nullptr, {.map = map});
}

} // namespace nw::toolset
