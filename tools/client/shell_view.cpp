#include "shell_view.hpp"
#include "client_input.hpp"
#include "shell_controller.hpp"
#include "toolset_backend.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <vector>

namespace nw::toolset {
namespace {

constexpr int kBottomDockViewportReservePx = 96;

Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id)
{
    for (; element; element = element->GetParentNode()) {
        if (element->GetId() == id) { return element; }
    }
    return nullptr;
}
Rml::Element* find_ancestor_with_class(Rml::Element* element, const char* name)
{
    for (; element; element = element->GetParentNode()) {
        if (element->IsClassSet(name)) { return element; }
    }
    return nullptr;
}

std::pair<int, int> query_window_size(SDL_Window* window)
{
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    return {window_w, window_h};
}

std::string escape_html(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::string get_input_value(Rml::ElementDocument* doc, const char* id)
{
    if (!doc) {
        return {};
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            return control->GetValue();
        }
        return input->GetAttribute<Rml::String>("value", "");
    }
    return {};
}

std::optional<size_t> parse_size(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

void set_input_value_and_cursor(Rml::ElementDocument* doc, const char* id, std::string_view value, size_t cursor_byte_position)
{
    if (!doc) {
        return;
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
            const Rml::String rml_value(value);
            control->SetValue(rml_value);
            const int cursor = Rml::StringUtilities::ConvertByteOffsetToCharacterOffset(
                rml_value,
                static_cast<int>(std::min(cursor_byte_position, rml_value.size())));
            control->SetSelectionRange(cursor, cursor);
            return;
        }
        input->SetAttribute("value", Rml::String(value));
    }
}

bool get_input_cursor_byte_position(Rml::ElementDocument* doc, const char* id, std::string_view value, size_t& cursor_byte_position)
{
    if (!doc) {
        return false;
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
            int selection_start = 0;
            int selection_end = 0;
            Rml::String selected_text;
            control->GetSelection(&selection_start, &selection_end, &selected_text);
            if (selection_start != selection_end) {
                return false;
            }

            const Rml::String rml_value(value);
            const int byte_offset = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(rml_value, selection_start);
            cursor_byte_position = byte_offset < 0
                ? 0
                : std::min(static_cast<size_t>(byte_offset), rml_value.size());
            return true;
        }
    }
    return false;
}

int bottom_dock_available_height_px(SDL_Window* window)
{
    const auto window_size = query_window_size(window);
    const int window_height = window_size.second > 0 ? window_size.second : 720;
    return std::max(1, window_height - kBottomDockViewportReservePx);
}

int left_dock_available_width_px(SDL_Window* window)
{
    const auto window_size = query_window_size(window);
    const int window_width = window_size.first > 0 ? window_size.first : 1280;
    return std::max(1, window_width - kBottomDockViewportReservePx);
}

int clamp_left_dock_width_px(const ShellController& shell, int width_px, SDL_Window* window)
{
    const auto& left = shell.docks.pane(nw::toolset::DockRegion::left);
    const int available_width = left_dock_available_width_px(window);
    const int min_width = std::min(left.min_size_px, available_width);
    const int max_width = std::max(min_width, std::min(left.max_size_px, available_width));
    return std::clamp(width_px, min_width, max_width);
}

int clamp_bottom_dock_height_px(const ShellController& shell, int height_px, SDL_Window* window)
{
    const auto& bottom = shell.docks.pane(nw::toolset::DockRegion::bottom);
    const int available_height = bottom_dock_available_height_px(window);
    const int min_height = std::min(bottom.min_size_px, available_height);
    const int max_height = std::max(min_height, std::min(bottom.max_size_px, available_height));
    return std::clamp(height_px, min_height, max_height);
}

// Round the delta before adding the integer start size, retaining negative
// half-pixel behavior. Positive requests are bounded before conversion; zero
// keeps apply_*'s existing nonpositive-request no-op policy.
int bounded_dock_resize_request(double requested)
{
    return static_cast<int>(std::clamp(requested, 0.0, static_cast<double>(std::numeric_limits<int>::max())));
}

bool left_dock_visible_for_active_tab(const ShellController& shell)
{
    return shell.showing_project_tree || shell.showing_areas;
}

} // namespace

void apply_shell_layout(Rml::ElementDocument* doc, const ShellController& shell, ShellPreviewLayout preview)
{
    if (!doc) {
        return;
    }

    const bool play_preview_active = preview.active;
    doc->SetClass("play_preview_active", play_preview_active);
    doc->SetClass("play_preview_placement_pending",
        preview.placement_pending);
    const auto& left = shell.docks.pane(nw::toolset::DockRegion::left);
    const auto& bottom = shell.docks.pane(nw::toolset::DockRegion::bottom);
    const int panel_bottom_px = bottom.visible ? bottom.size_px : 0;
    const bool show_left_dock = !play_preview_active
        && left_dock_visible_for_active_tab(shell);
    if (auto* panel = doc->GetElementById("panel")) {
        panel->SetProperty("display", show_left_dock ? "flex" : "none");
        panel->SetProperty("width", std::to_string(left.size_px) + "px");
        panel->SetProperty("bottom", std::to_string(panel_bottom_px) + "px");
    }
    if (auto* workspace = doc->GetElementById("workspace_shell")) {
        workspace->SetProperty("left", std::to_string(show_left_dock ? left.size_px : 0) + "px");
        workspace->SetProperty("bottom", std::to_string(play_preview_active ? 0 : panel_bottom_px) + "px");
    }
}

void apply_left_dock_width(Rml::ElementDocument* doc, ShellController& shell, SDL_Window* window, int requested_width_px, ShellPreviewLayout preview)
{
    if (!doc || requested_width_px <= 0) {
        return;
    }

    const int width_px = clamp_left_dock_width_px(shell, requested_width_px, window);
    shell.docks.set_size_px(nw::toolset::DockRegion::left, width_px);
    apply_shell_layout(doc, shell, preview);
}

void apply_bottom_dock_height(Rml::ElementDocument* doc, ShellController& shell, SDL_Window* window, int requested_height_px, ShellPreviewLayout preview)
{
    if (!doc || requested_height_px <= 0) {
        return;
    }

    const int height_px = clamp_bottom_dock_height_px(shell, requested_height_px, window);
    shell.set_bottom_dock_size_px(height_px);

    if (auto* dock = doc->GetElementById("bottom_dock")) {
        dock->SetProperty("height", std::to_string(height_px) + "px");
    }
    apply_shell_layout(doc, shell, preview);
}

bool begin_bottom_dock_resize(Rml::Context* context,
    SDL_Window* window,
    Rml::ElementDocument* doc,
    ShellViewState& state, ShellController& shell,
    const SDL_MouseButtonEvent& mouse)
{
    if (!shell.bottom_dock_visible() || mouse.button != SDL_BUTTON_LEFT || !context || !doc) {
        return false;
    }

    const auto point = to_context_point(window, mouse.x, mouse.y);
    auto* hit = context->GetElementAtPoint(point);
    if (!find_ancestor_with_id(hit, "bottom_dock_resize_grabber")) {
        return false;
    }

    auto* dock = doc->GetElementById("bottom_dock");
    if (!dock) {
        return false;
    }

    const auto& bottom = shell.docks.pane(nw::toolset::DockRegion::bottom);
    const double measured = std::round(static_cast<double>(dock->GetOffsetHeight()));
    const int measured_height = std::isfinite(measured) && measured > 0.0
        ? bounded_dock_resize_request(measured)
        : 0;
    state.bottom_dock_resizing = true;
    state.bottom_dock_resize_start_y = point.y;
    state.bottom_dock_resize_start_height_px = measured_height > 0
        ? measured_height
        : clamp_bottom_dock_height_px(shell, bottom.size_px, window);
    shell.set_bottom_dock_size_px(state.bottom_dock_resize_start_height_px);
    SDL_CaptureMouse(true);
    return true;
}

bool update_bottom_dock_resize(Rml::ElementDocument* doc, ShellViewState& state, ShellController& shell, SDL_Window* window, const SDL_MouseMotionEvent& motion, ShellPreviewLayout preview)
{
    if (!state.bottom_dock_resizing) {
        return false;
    }

    const auto point = to_context_point(window, motion.x, motion.y);
    if (!std::isfinite(point.y) || !std::isfinite(state.bottom_dock_resize_start_y)) {
        (void)end_bottom_dock_resize(state);
        return false;
    }
    const double requested = static_cast<double>(state.bottom_dock_resize_start_height_px)
        - std::round(static_cast<double>(point.y) - static_cast<double>(state.bottom_dock_resize_start_y));
    const int requested_height = bounded_dock_resize_request(requested);
    apply_bottom_dock_height(doc, shell, window, requested_height, preview);
    return true;
}

bool end_bottom_dock_resize(ShellViewState& state)
{
    if (!state.bottom_dock_resizing) {
        return false;
    }

    state.bottom_dock_resizing = false;
    SDL_CaptureMouse(false);
    return true;
}

bool begin_left_dock_resize(Rml::Context* context,
    SDL_Window* window,
    Rml::ElementDocument* doc,
    ShellViewState& state, ShellController& shell,
    const SDL_MouseButtonEvent& mouse)
{
    if (mouse.button != SDL_BUTTON_LEFT || !context || !doc) {
        return false;
    }

    const auto point = to_context_point(window, mouse.x, mouse.y);
    auto* hit = context->GetElementAtPoint(point);
    if (!find_ancestor_with_id(hit, "left_dock_resize_grabber")) {
        return false;
    }

    auto* panel = doc->GetElementById("panel");
    if (!panel) {
        return false;
    }

    const auto& left = shell.docks.pane(nw::toolset::DockRegion::left);
    const double measured = std::round(static_cast<double>(panel->GetOffsetWidth()));
    const int measured_width = std::isfinite(measured) && measured > 0.0
        ? bounded_dock_resize_request(measured)
        : 0;
    state.left_dock_resizing = true;
    state.left_dock_resize_start_x = point.x;
    state.left_dock_resize_start_width_px = measured_width > 0
        ? measured_width
        : clamp_left_dock_width_px(shell, left.size_px, window);
    shell.docks.set_size_px(nw::toolset::DockRegion::left, state.left_dock_resize_start_width_px);
    SDL_CaptureMouse(true);
    return true;
}

bool update_left_dock_resize(Rml::ElementDocument* doc, ShellViewState& state, ShellController& shell, SDL_Window* window, const SDL_MouseMotionEvent& motion, ShellPreviewLayout preview)
{
    if (!state.left_dock_resizing) {
        return false;
    }

    const auto point = to_context_point(window, motion.x, motion.y);
    if (!std::isfinite(point.x) || !std::isfinite(state.left_dock_resize_start_x)) {
        (void)end_left_dock_resize(state);
        return false;
    }
    const double requested = static_cast<double>(state.left_dock_resize_start_width_px)
        + std::round(static_cast<double>(point.x) - static_cast<double>(state.left_dock_resize_start_x));
    const int requested_width = bounded_dock_resize_request(requested);
    apply_left_dock_width(doc, shell, window, requested_width, preview);
    return true;
}

bool end_left_dock_resize(ShellViewState& state)
{
    if (!state.left_dock_resizing) {
        return false;
    }

    state.left_dock_resizing = false;
    SDL_CaptureMouse(false);
    return true;
}

bool consume_terminal_toggle_text_input(ShellViewState& state, const SDL_Event& event)
{
    if (!state.suppress_terminal_toggle_text_input) {
        return false;
    }

    if (event.type == SDL_EVENT_TEXT_INPUT) {
        state.suppress_terminal_toggle_text_input = false;
        const std::string_view text = event.text.text;
        return text == "`" || text == "~";
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key != SDLK_GRAVE) {
        state.suppress_terminal_toggle_text_input = false;
    }

    return false;
}

bool output_scroll_input(Rml::ElementDocument* doc, Rml::Context* context,
    SDL_Window* window, const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        return focused_element_has_id(context, "output_list");
    case SDL_EVENT_MOUSE_WHEEL:
        return point_within_element(doc, "output_list",
            to_context_point(window, event.wheel.mouse_x, event.wheel.mouse_y));
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return point_within_element(doc, "output_list",
            to_context_point(window, event.button.x, event.button.y));
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return focused_element_has_id(context, "output_list")
            || point_within_element(doc, "output_list",
                to_context_point(window, event.button.x, event.button.y));
    case SDL_EVENT_MOUSE_MOTION:
        return (event.motion.state & SDL_BUTTON_LMASK) != 0
            && (focused_element_has_id(context, "output_list")
                || point_within_element(doc, "output_list",
                    to_context_point(window, event.motion.x, event.motion.y)));
    default:
        return false;
    }
}

void observe_output_scroll(Rml::ElementDocument* doc, ShellController& shell)
{
    if (auto* output = doc ? doc->GetElementById("output_list") : nullptr) {
        shell.observe_output_scroll(
            output->GetScrollTop(), output->GetScrollHeight(), output->GetClientHeight());
    }
}

bool apply_output_scroll_after_layout(Rml::ElementDocument* doc, ShellViewState& state, ShellController& shell)
{
    if (state.output_scroll_after_layout == OutputScrollAfterLayout::none
        || !shell.output_panel_visible()) {
        return false;
    }

    auto* output = doc ? doc->GetElementById("output_list") : nullptr;
    if (!output || !output->IsVisible(true)) {
        return false;
    }

    const float previous_scroll_top = output->GetScrollTop();
    if (state.output_scroll_after_layout == OutputScrollAfterLayout::follow_tail) {
        output->SetScrollTop(output->GetScrollHeight());
    }
    observe_output_scroll(doc, shell);
    state.output_scroll_after_layout = OutputScrollAfterLayout::none;
    return output->GetScrollTop() != previous_scroll_top;
}

namespace {

size_t output_byte_offset_at_x(Rml::Element* row, std::string_view text, float x)
{
    if (!row || text.empty() || x <= 0.0f) {
        return 0;
    }

    size_t previous_byte = 0;
    float width = 0.0f;
    while (previous_byte < text.size()) {
        const char* next = Rml::StringUtilities::SeekForwardUTF8(
            text.data() + previous_byte + 1, text.data() + text.size());
        const size_t current_byte = static_cast<size_t>(next - text.data());
        const float glyph_width = static_cast<float>(Rml::ElementUtilities::GetStringWidth(
            row, Rml::StringView{text.data() + previous_byte, next}));
        if (x < width + glyph_width * 0.5f) {
            return previous_byte;
        }
        previous_byte = current_byte;
        width += glyph_width;
    }
    return text.size();
}

} // namespace

std::optional<size_t> output_text_offset_at_point(
    Rml::Context* context, Rml::ElementDocument* doc,
    const ShellViewState& state, Rml::Vector2f point)
{
    if (!context || !point_within_element(doc, "output_list", point)) {
        return std::nullopt;
    }

    auto* row = find_ancestor_with_class(
        context->GetElementAtPoint(point), "output_line");
    if (!row) {
        return std::nullopt;
    }

    const auto start = parse_size(
        row->GetAttribute<Rml::String>("data-output-start", ""));
    const auto length = parse_size(
        row->GetAttribute<Rml::String>("data-output-length", ""));
    if (!start || !length || *start > state.output_selection.text.size()) {
        return std::nullopt;
    }

    const size_t clamped_length = std::min(
        *length, state.output_selection.text.size() - *start);
    const std::string_view row_text{state.output_selection.text.data() + *start,
        clamped_length};
    const float local_x = point.x - row->GetAbsoluteOffset(Rml::BoxArea::Content).x;
    return *start + output_byte_offset_at_x(row, row_text, local_x);
}

void refresh_terminal_view(Rml::ElementDocument* doc, const ShellController& shell)
{
    if (!doc) {
        return;
    }

    std::string markup;
    for (const auto& [style, line] : shell.terminal_lines) {
        markup += "<div class=\"terminal_line ";
        markup += escape_html(style);
        markup += "\">";
        markup += escape_html(line);
        markup += "</div>";
    }

    if (auto* lines = doc->GetElementById("terminal_output_lines")) {
        lines->SetInnerRML(markup);
    } else if (auto* output = doc->GetElementById("terminal_output")) {
        output->SetInnerRML(markup);
    }

    if (auto* output = doc->GetElementById("terminal_output")) {
        output->SetScrollTop(output->GetScrollHeight());
    }
}

void refresh_bottom_dock_view(Rml::ElementDocument* doc, const ShellController& shell, ShellPreviewLayout preview)
{
    if (!doc) {
        return;
    }

    const auto& bottom = shell.docks.pane(nw::toolset::DockRegion::bottom);
    const bool terminal_active = shell.terminal_visible();
    const bool output_active = shell.output_panel_visible();

    if (auto* dock = doc->GetElementById("bottom_dock")) {
        dock->SetClass("visible", bottom.visible);
        dock->SetClass("output_active", output_active);
        dock->SetClass("terminal_active", terminal_active);
    }
    if (auto* output = doc->GetElementById("output_panel")) {
        output->SetClass("visible", output_active);
    }
    if (auto* terminal = doc->GetElementById("terminal_panel")) {
        terminal->SetClass("visible", terminal_active);
    }
    if (auto* tab = doc->GetElementById("bottom_tab_output")) {
        tab->SetClass("active", output_active);
    }
    if (auto* tab = doc->GetElementById("bottom_tab_terminal")) {
        tab->SetClass("active", terminal_active);
    }

    apply_shell_layout(doc, shell, preview);

    if (terminal_active) {
        refresh_terminal_view(doc, shell);
        if (auto* input = doc->GetElementById("terminal_input")) {
            input->Focus();
        }
    }
}

void refresh_output_view(Rml::ElementDocument* doc, ShellViewState& state, const ShellController& shell)
{
    if (!doc) {
        return;
    }

    const std::string filter = get_input_value(doc, "output_filter");
    const std::string lowered_filter = [&]() {
        std::string v = filter;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v;
    }();

    struct OutputViewRow {
        std::string_view channel;
        std::string_view text;
        size_t start = 0;
    };

    std::vector<OutputViewRow> rows;
    std::string text;
    bool has_rows = false;
    for (const auto& [channel, line] : shell.output_lines) {
        if (!shell.output_channel_visible(channel)) {
            continue;
        }

        std::string lowered_line = line;
        std::transform(lowered_line.begin(), lowered_line.end(), lowered_line.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!lowered_filter.empty() && lowered_line.find(lowered_filter) == std::string::npos) {
            continue;
        }

        size_t segment_start = 0;
        while (true) {
            const size_t segment_end = line.find('\n', segment_start);
            const size_t segment_size = segment_end == std::string::npos
                ? line.size() - segment_start
                : segment_end - segment_start;
            const std::string_view segment{line.data() + segment_start, segment_size};
            if (has_rows) {
                text.push_back('\n');
            }
            const size_t row_start = text.size();
            text.append(segment);
            rows.push_back(OutputViewRow{channel, segment, row_start});
            has_rows = true;

            if (segment_end == std::string::npos) {
                break;
            }
            segment_start = segment_end + 1;
        }
    }

    const bool preserves_offsets = text.size() >= state.output_selection.text.size()
        && text.compare(0, state.output_selection.text.size(), state.output_selection.text) == 0;
    if (!preserves_offsets) {
        state.output_selection.clear();
    }
    state.output_selection.text = std::move(text);
    state.output_selection.anchor = std::min(
        state.output_selection.anchor, state.output_selection.text.size());
    state.output_selection.focus = std::min(
        state.output_selection.focus, state.output_selection.text.size());

    const auto [selection_start, selection_end] = state.output_selection.range();
    std::string markup;
    for (const auto& row : rows) {
        markup += "<div class=\"output_line ";
        markup += escape_html(row.channel);
        markup += "\" data-output-start=\"";
        markup += std::to_string(row.start);
        markup += "\" data-output-length=\"";
        markup += std::to_string(row.text.size());
        markup += "\">";

        const size_t row_end = row.start + row.text.size();
        const size_t selected_start = std::clamp(selection_start, row.start, row_end);
        const size_t selected_end = std::clamp(selection_end, row.start, row_end);
        const size_t local_start = selected_start - row.start;
        const size_t local_end = selected_end - row.start;
        markup += escape_html(row.text.substr(0, local_start));
        if (local_start < local_end) {
            markup += "<span class=\"output_text_selection\">";
            markup += escape_html(row.text.substr(local_start, local_end - local_start));
            markup += "</span>";
        }
        markup += escape_html(row.text.substr(local_end));
        markup += "</div>";
    }

    if (auto* output = doc->GetElementById("output_list")) {
        const float scroll_top = output->GetScrollTop();
        const bool follow_tail = shell.output_follows_tail();

        if (auto* lines = doc->GetElementById("output_list_lines")) {
            lines->SetInnerRML(markup);
        }
        if (!follow_tail) {
            output->SetScrollTop(scroll_top);
        }
        state.output_scroll_after_layout = follow_tail
            ? OutputScrollAfterLayout::follow_tail
            : OutputScrollAfterLayout::observe;
    }

    const char* ids[] = {"output_info", "output_warn", "output_error", "output_script"};
    const char* values[] = {"info", "warn", "error", "script"};
    for (size_t i = 0; i < 4; ++i) {
        if (auto* btn = doc->GetElementById(ids[i])) {
            btn->SetClass("active", shell.output_channel_visible(values[i]));
        }
    }
}

namespace {

std::string format_command_candidates(const std::vector<nw::toolset::CommandSpec>& candidates)
{
    constexpr size_t max_listed = 8;
    std::string line = "Matches:";
    const size_t count = std::min(candidates.size(), max_listed);
    for (size_t i = 0; i < count; ++i) {
        line += (i == 0) ? " " : ", ";
        line += candidates[i].id;
    }
    if (candidates.size() > max_listed) {
        line += ", ...";
    }
    return line;
}

} // namespace

bool complete_terminal_command(Rml::ElementDocument* doc, ShellController& shell, const ToolsetBackend& backend)
{
    if (!doc) {
        return false;
    }

    const std::string line = get_input_value(doc, "terminal_input");
    size_t cursor_byte_position = 0;
    if (!get_input_cursor_byte_position(doc, "terminal_input", line, cursor_byte_position)) {
        return false;
    }

    const auto completion = backend.complete_console_command(line, cursor_byte_position);
    if (completion.completed) {
        set_input_value_and_cursor(doc, "terminal_input", completion.replacement, completion.cursor_byte_position);
    }
    if (completion.ambiguous && !completion.candidates.empty()) {
        shell.append_terminal("info", format_command_candidates(completion.candidates));
    }
    return completion.completed || !completion.candidates.empty();
}

std::optional<ShellUiClick> capture_shell_ui_click(Rml::Element* hit)
{
    if (auto* dock = find_ancestor_with_class(hit, "dock_tab")) {
        return ShellUiClick{ShellUiClickKind::dock, dock->GetAttribute<Rml::String>("data-widget", "")};
    }
    auto* output = find_ancestor_with_class(hit, "output_toggle");
    if (!output) { return std::nullopt; }
    ShellUiClick click;
    const auto& id = output->GetId();
    if (id == "output_info") {
        click.value = "info";
    } else if (id == "output_warn") {
        click.value = "warn";
    } else if (id == "output_error") {
        click.value = "error";
    } else if (id == "output_script") {
        click.value = "script";
    }
    if (!click.value.empty()) { click.kind = ShellUiClickKind::output_channel; }
    return click;
}

std::optional<CommandInvocation> take_shell_ui_click_command(ShellUiClick& click)
{
    const auto kind = std::exchange(click.kind, ShellUiClickKind::none);
    if (click.value.empty()) { return std::nullopt; }
    CommandInvocation command;
    if (kind == ShellUiClickKind::dock) {
        command.command_id = "rollnw.client.dock.activate";
        command.args.push_back(CommandArg::positional_string("bottom"));
    } else if (kind == ShellUiClickKind::output_channel) {
        command.command_id = "rollnw.client.output.channel";
    } else {
        return std::nullopt;
    }
    command.args.push_back(CommandArg::positional_string(std::move(click.value)));
    return command;
}

ShellOutputKeyResult handle_shell_output_key(const SDL_KeyboardEvent& key,
    Rml::Context* context, ShellViewState& state, ShellController& shell)
{
    if (key.repeat || !focused_element_has_id(context, "output_list")
        || !(key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) || (key.mod & SDL_KMOD_ALT)) { return {}; }
    if (key.key == SDLK_A) {
        state.output_selection.anchor = 0;
        state.output_selection.focus = state.output_selection.text.size();
        shell.output_dirty = true;
        return {.handled = true};
    }
    if (key.key != SDLK_C) { return {}; }
    const auto [start, end] = state.output_selection.range();
    const auto first = std::min(start, state.output_selection.text.size());
    const auto last = std::min(end, state.output_selection.text.size());
    ShellOutputKeyResult result{.handled = true};
    if (first < last) { result.clipboard = state.output_selection.text.substr(first, last - first); }
    return result;
}

} // namespace nw::toolset
