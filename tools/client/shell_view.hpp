#pragma once

#include "command_bus.hpp"

#include <RmlUi/Core/Types.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace loguru {
struct Message;
}

namespace Rml {
class Context;
class Element;
class ElementDocument;
}

namespace nw::toolset {

class ShellController;
class ToolsetBackend;

struct CapturedLogLine {
    std::string channel;
    std::string message;
};
// One active process callback, stable address until synchronized SDK removal.
// Borrowed messages become owning FIFO rows; cap=512 drops oldest, empty rows
// and callback allocation failures drop. Drains transfer the ordered row batch.
class LoguruOutputCapture {
public:
    LoguruOutputCapture();
    ~LoguruOutputCapture();
    LoguruOutputCapture(const LoguruOutputCapture&) = delete;
    LoguruOutputCapture& operator=(const LoguruOutputCapture&) = delete;
    std::vector<CapturedLogLine> drain();

private:
    void push(const loguru::Message& message);
    static void handle_log(void* user_data, const loguru::Message& message) noexcept;
    std::mutex mutex_;
    std::deque<CapturedLogLine> lines_;
};
void flush_shell_log_capture(LoguruOutputCapture& capture, ShellController& shell);

struct OutputSelectionState {
    std::string text;
    size_t anchor = 0;
    size_t focus = 0;
    bool dragging = false;

    [[nodiscard]] bool active() const noexcept { return anchor != focus; }
    [[nodiscard]] std::pair<size_t, size_t> range() const noexcept { return std::minmax(anchor, focus); }
    void clear() noexcept
    {
        anchor = 0;
        focus = 0;
        dragging = false;
    }
};

enum class OutputScrollAfterLayout : uint8_t { none,
    observe,
    follow_tail };

// One displayed shell owns captured dock resize, output presentation and the
// configuration path. Controller rows/panes and browser history retain owners.
struct ShellViewState {
    std::filesystem::path preferences_path;
    std::string last_output_filter;
    std::vector<CommandPromptAction> new_resource_actions;
    OutputSelectionState output_selection;
    OutputScrollAfterLayout output_scroll_after_layout = OutputScrollAfterLayout::none;
    bool suppress_terminal_toggle_text_input = false;
    bool bottom_dock_resizing = false;
    bool left_dock_resizing = false;
    float bottom_dock_resize_start_y = 0.0f;
    int bottom_dock_resize_start_height_px = 0;
    float left_dock_resize_start_x = 0.0f;
    int left_dock_resize_start_width_px = 0;
};

// Current facts only: active means session active or placement pending. No
// preview owner/lifetime crosses this synchronous layout protocol.
struct ShellPreviewLayout {
    bool active = false;
    bool placement_pending = false;
};

enum class ShellUiClickKind : uint8_t { none,
    dock,
    output_channel,
    new_resource,
    new_resource_action };
// Schema revision 1, singleton shell click with owning attribute text. No SDK
// borrow escapes. A matched empty/unknown toggle produces no command.
struct ShellUiClick {
    ShellUiClickKind kind = ShellUiClickKind::none;
    std::string value;
};
std::optional<ShellUiClick> capture_shell_ui_click(Rml::Element* hit);
// Consume once; unsupported nonempty dock values retain backend rejection.
std::optional<CommandInvocation> take_shell_ui_click_command(ShellUiClick& click);
// The project menu owns a compact copy of the current resource actions. DOM
// rows carry only indices into that batch; stale and out-of-range indices reject.
bool open_project_new_resource_menu(Rml::ElementDocument* doc,
    ShellViewState& state, const CommandPrompt& prompt);
bool close_project_new_resource_menu(Rml::ElementDocument* doc,
    ShellViewState& state);
[[nodiscard]] bool project_new_resource_menu_contains(Rml::Element* hit);
std::optional<CommandPromptAction> take_project_new_resource_action(
    ShellUiClick& click, const ShellViewState& state);

struct ShellOutputKeyResult {
    bool handled = false;
    std::optional<std::string> clipboard;
};
// One displayed output's current visible focus and byte selection. Repeat/Alt/
// unmatched keys reject; selection ranges clamp to the current flattened text.
ShellOutputKeyResult handle_shell_output_key(const SDL_KeyboardEvent& key,
    Rml::Context* context, ShellViewState& state, ShellController& shell);

void apply_shell_layout(Rml::ElementDocument* doc, const ShellController& shell, ShellPreviewLayout preview);
void apply_left_dock_width(Rml::ElementDocument* doc, ShellController& shell,
    SDL_Window* window, int requested_width_px, ShellPreviewLayout preview);
void apply_bottom_dock_height(Rml::ElementDocument* doc, ShellController& shell,
    SDL_Window* window, int requested_height_px, ShellPreviewLayout preview);
// Native pointer coordinates are validated by client_input before these calls.
// Pane sizes clamp to the available window; non-positive requests are ignored.
[[nodiscard]] bool begin_bottom_dock_resize(Rml::Context* context, SDL_Window* window,
    Rml::ElementDocument* doc, ShellViewState& state, ShellController& shell,
    const SDL_MouseButtonEvent& mouse);
[[nodiscard]] bool begin_left_dock_resize(Rml::Context* context, SDL_Window* window,
    Rml::ElementDocument* doc, ShellViewState& state, ShellController& shell,
    const SDL_MouseButtonEvent& mouse);
[[nodiscard]] bool update_bottom_dock_resize(Rml::ElementDocument* doc, ShellViewState& state,
    ShellController& shell, SDL_Window* window, const SDL_MouseMotionEvent& motion, ShellPreviewLayout preview);
[[nodiscard]] bool update_left_dock_resize(Rml::ElementDocument* doc, ShellViewState& state,
    ShellController& shell, SDL_Window* window, const SDL_MouseMotionEvent& motion, ShellPreviewLayout preview);
// Release reports whether root should persist the current pane/history batch.
[[nodiscard]] bool end_bottom_dock_resize(ShellViewState& state);
[[nodiscard]] bool end_left_dock_resize(ShellViewState& state);
[[nodiscard]] bool consume_terminal_toggle_text_input(ShellViewState& state, const SDL_Event& event);

void refresh_terminal_view(Rml::ElementDocument* doc, const ShellController& shell);
void refresh_bottom_dock_view(Rml::ElementDocument* doc, const ShellController& shell, ShellPreviewLayout preview);
void refresh_output_view(Rml::ElementDocument* doc, ShellViewState& state, const ShellController& shell);
// Singleton displayed filter: missing field means empty, unchanged does no work.
void refresh_output_filter(Rml::ElementDocument*, ShellViewState&, ShellController&);
void observe_output_scroll(Rml::ElementDocument* doc, ShellController& shell);
[[nodiscard]] bool apply_output_scroll_after_layout(Rml::ElementDocument* doc,
    ShellViewState& state, ShellController& shell);
[[nodiscard]] bool output_scroll_input(Rml::ElementDocument* doc, Rml::Context* context,
    SDL_Window* window, const SDL_Event& event);
// Selection is in bytes of this view's flattened visible text. Malformed/missing
// row metadata is rejected; row lengths are clamped to current text bounds.
[[nodiscard]] std::optional<size_t> output_text_offset_at_point(Rml::Context* context,
    Rml::ElementDocument* doc, const ShellViewState& state, Rml::Vector2f point);
[[nodiscard]] bool complete_terminal_command(Rml::ElementDocument* doc,
    ShellController& shell, const ToolsetBackend& backend);

} // namespace nw::toolset
