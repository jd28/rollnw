#pragma once

#include "../ui/virtual_combobox.hpp"
#include "command_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_KeyboardEvent;

namespace Rml {
class Context;
class Element;
class ElementDocument;
}

namespace nw::toolset {

class ToolsetBackend;
class ShellController;

// One command overlay's transient presentation. The document belongs to the Rml
// context and outlives these synchronous calls; the prompt owns values/choices.
// Generation changes replace markup and invalidate its popup/DOM borrows.
struct CommandViewState {
    std::string last_command_query;
    std::vector<CommandSpec> commands;
    std::string command_palette_restore_focus_id;
    bool command_palette_ui_visible = false;
    bool command_palette_restore_captured = false;
    bool command_palette_restore_viewport_focus = false;
    std::optional<nw::toolset::CommandPrompt> command_form;
    Rml::ElementDocument* command_overlay_document = nullptr; // Borrowed from the command context.
    uint64_t command_form_generation = 0;
    uint64_t rendered_command_form_generation = 0;
    uint64_t command_form_browse_generation = 0;
    nw::toolset::VirtualComboBox command_form_combobox;
    std::optional<size_t> command_form_combobox_field;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement> command_form_combobox_placement;
    size_t blueprint_review_page = 0;
    std::string blueprint_operation_markup;
};

// The root sets shell visibility before this call, then initializes the backend,
// refreshes matches, and focuses the palette input when opening. Capturing focus
// is idempotent until close; hidden/missing restore IDs are ignored. Visibility
// changes are resolved before returning so input routing needs no later frame.
[[nodiscard]] Rml::ElementDocument* load_command_palette_document(Rml::Context& context);
void set_command_palette_visibility(CommandViewState& state, Rml::Context* context,
    Rml::Context* palette_context, Rml::ElementDocument* document,
    Rml::ElementDocument* palette_document, bool& viewport_focused, bool visible);
void refresh_command_palette(Rml::ElementDocument* document,
    CommandViewState& state, const ToolsetBackend& backend);
void refresh_command_palette_query(Rml::ElementDocument* document,
    CommandViewState& state, const ToolsetBackend& backend, bool visible);

enum class CommandOverlayActionKind : uint8_t {
    none,
    handled,
    dispatch,
    submit,
    browse_directory,
};

// No DOM borrow escapes this result. An index is used only for submit, and is
// validated against the current form by take_command_form_action.
struct CommandOverlayAction {
    CommandOverlayActionKind kind = CommandOverlayActionKind::none;
    std::string command_id;
    size_t form_action_index = 0;
};

[[nodiscard]] CommandOverlayAction handle_command_overlay_target(
    CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active, Rml::Element* target);

// One overlay owns keyboard focus and choice presentation. No DOM borrow escapes
// this synchronous singleton call. A returned action index is validated again by
// take_command_form_action; unhandled keys retain ordinary SDK forwarding.
struct CommandFormKeyResult {
    bool handled = false;
    std::optional<size_t> action_index;
};
[[nodiscard]] CommandFormKeyResult handle_command_form_key(
    CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active, Rml::Context* context, const SDL_KeyboardEvent& key);
[[nodiscard]] std::optional<CommandPromptAction> take_command_form_action(
    CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active, bool dialog_open, size_t index);
// Returns false for a stale/absent form. Matching cancellation still advances
// the generation, as does the original native-dialog result path.
[[nodiscard]] bool apply_command_form_directory_result(CommandViewState& state,
    const std::string& path, const std::string& error, bool canceled);
[[nodiscard]] bool take_command_form_prompt(CommandViewState& state, CommandResult& result);
[[nodiscard]] std::optional<CommandPromptAction> show_command_prompt(
    SDL_Window* window, const CommandPrompt& prompt);
void append_command_results(ShellController& shell, std::span<const CommandResult> results);
void append_terminal_results(ShellController& shell, std::span<const CommandResult> results);

void sync_command_overlay_visibility(CommandViewState& state,
    bool project_load_active, bool operation_active);
void close_command_form_combobox(CommandViewState& state);
[[nodiscard]] bool open_command_form_combobox(CommandViewState& state, size_t field_index);
void sync_command_form_combobox(CommandViewState& state, bool force = false);
void sync_command_form(CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active, bool force = false);
[[nodiscard]] bool commit_command_form_combobox(CommandViewState& state,
    const ToolsetBackend& backend, bool project_load_active, int32_t choice_index);
void sync_blueprint_operation(CommandViewState& state, const ToolsetBackend& backend,
    bool project_load_active);

} // namespace nw::toolset
