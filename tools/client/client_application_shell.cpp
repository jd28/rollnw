#include "client_application_shell.hpp"
#include "client_application_commands.hpp"
#include "client_preferences.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <utility>

namespace nw::toolset::client_application_detail {
nw::toolset::ShellPreviewLayout shell_preview_layout(const ClientApplicationState& state)
{
    const bool pending = state.play_preview.placement_pending();
    return {.active = state.play_preview.session.active() || pending, .placement_pending = pending};
}

void apply_shell_layout(Rml::ElementDocument* doc, const ClientApplicationState& state)
{
    nw::toolset::apply_shell_layout(doc, state.shell, shell_preview_layout(state));
}

void apply_left_dock_width(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, int requested_width_px)
{
    nw::toolset::apply_left_dock_width(doc, state.shell, window, requested_width_px, shell_preview_layout(state));
}

void apply_bottom_dock_height(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, int requested_height_px)
{
    nw::toolset::apply_bottom_dock_height(doc, state.shell, window, requested_height_px, shell_preview_layout(state));
}

bool begin_bottom_dock_resize(Rml::Context* context, SDL_Window* window, Rml::ElementDocument* doc,
    ClientApplicationState& state, const SDL_MouseButtonEvent& mouse)
{
    return nw::toolset::begin_bottom_dock_resize(context, window, doc, state.shell_view, state.shell, mouse);
}

bool update_bottom_dock_resize(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion)
{
    return nw::toolset::update_bottom_dock_resize(doc, state.shell_view, state.shell, window, motion, shell_preview_layout(state));
}

bool end_bottom_dock_resize(ClientApplicationState& state)
{
    if (!nw::toolset::end_bottom_dock_resize(state.shell_view)) { return false; }
    save_ui_preferences(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects);
    return true;
}

bool begin_left_dock_resize(Rml::Context* context, SDL_Window* window, Rml::ElementDocument* doc,
    ClientApplicationState& state, const SDL_MouseButtonEvent& mouse)
{
    return nw::toolset::begin_left_dock_resize(context, window, doc, state.shell_view, state.shell, mouse);
}

bool update_left_dock_resize(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion)
{
    return nw::toolset::update_left_dock_resize(doc, state.shell_view, state.shell, window, motion, shell_preview_layout(state));
}

bool end_left_dock_resize(ClientApplicationState& state)
{
    if (!nw::toolset::end_left_dock_resize(state.shell_view)) { return false; }
    save_ui_preferences(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects);
    return true;
}

bool consume_terminal_toggle_text_input(ClientApplicationState& state, const SDL_Event& event)
{
    return nw::toolset::consume_terminal_toggle_text_input(state.shell_view, event);
}

void remember_recent_project(ClientApplicationState& state, const std::filesystem::path& project_dir)
{
    nw::toolset::remember_recent_project(state.shell_view.preferences_path, state.shell.docks, state.browser.recent_projects, project_dir);
}

void observe_output_scroll(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::observe_output_scroll(doc, state.shell);
}

bool apply_output_scroll_after_layout(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    return nw::toolset::apply_output_scroll_after_layout(doc, state.shell_view, state.shell);
}

std::optional<size_t> output_text_offset_at_point(Rml::Context* context,
    Rml::ElementDocument* doc, const ClientApplicationState& state, Rml::Vector2f point)
{
    return nw::toolset::output_text_offset_at_point(context, doc, state.shell_view, point);
}

void append_output(ClientApplicationState& state, std::string_view channel, std::string_view line)
{
    state.shell.append_output(channel, line);
}

void flush_log_capture(LoguruOutputCapture& capture, ClientApplicationState& state)
{
    nw::toolset::flush_shell_log_capture(capture, state.shell);
}

void append_terminal(ClientApplicationState& state, std::string_view style, std::string_view line)
{
    state.shell.append_terminal(style, line);
}

void refresh_terminal_view(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::refresh_terminal_view(doc, state.shell);
}

void refresh_bottom_dock_view(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::refresh_bottom_dock_view(doc, state.shell, shell_preview_layout(state));
}

void refresh_output_view(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::refresh_output_view(doc, state.shell_view, state.shell);
}

void toggle_terminal(Rml::ElementDocument* doc, ClientApplicationState& state, bool visible)
{
    state.shell.set_terminal_visible(visible);
    refresh_bottom_dock_view(doc, state);
}

void toggle_output_panel(Rml::ElementDocument* doc, ClientApplicationState& state, bool visible)
{
    state.shell.set_output_panel_visible(visible);
    refresh_bottom_dock_view(doc, state);
}

bool complete_terminal_command(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    return nw::toolset::complete_terminal_command(doc, state.shell, state.backend);
}

void sync_shell_visibility(Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientApplicationState& state)
{
    toggle_command_palette(context, palette_context, doc, palette_doc, state, state.shell.command_palette_visible);
    refresh_bottom_dock_view(doc, state);
}

} // namespace nw::toolset::client_application_detail
