#pragma once
#include "client_application_state.hpp"
#include "renderer.hpp"
#include <RmlUi_Platform_SDL.h>

namespace nw::toolset::client_application_detail {

// Private composition functions. Feature APIs receive only their own states.
nw::toolset::ShellPreviewLayout shell_preview_layout(const ClientApplicationState& state);

void apply_shell_layout(Rml::ElementDocument* doc, const ClientApplicationState& state);

void apply_left_dock_width(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, int requested_width_px);

void apply_bottom_dock_height(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, int requested_height_px);

bool begin_bottom_dock_resize(Rml::Context* context, SDL_Window* window, Rml::ElementDocument* doc,
    ClientApplicationState& state, const SDL_MouseButtonEvent& mouse);

bool update_bottom_dock_resize(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion);

bool end_bottom_dock_resize(ClientApplicationState& state);

bool begin_left_dock_resize(Rml::Context* context, SDL_Window* window, Rml::ElementDocument* doc,
    ClientApplicationState& state, const SDL_MouseButtonEvent& mouse);

bool update_left_dock_resize(Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Window* window, const SDL_MouseMotionEvent& motion);

bool end_left_dock_resize(ClientApplicationState& state);

bool consume_terminal_toggle_text_input(ClientApplicationState& state, const SDL_Event& event);

void remember_recent_project(ClientApplicationState& state, const std::filesystem::path& project_dir);

void observe_output_scroll(Rml::ElementDocument* doc, ClientApplicationState& state);

bool apply_output_scroll_after_layout(Rml::ElementDocument* doc, ClientApplicationState& state);

std::optional<size_t> output_text_offset_at_point(Rml::Context* context,
    Rml::ElementDocument* doc, const ClientApplicationState& state, Rml::Vector2f point);

void append_output(ClientApplicationState& state, std::string_view channel, std::string_view line);

void flush_log_capture(LoguruOutputCapture& capture, ClientApplicationState& state);

void append_terminal(ClientApplicationState& state, std::string_view style, std::string_view line);

void refresh_terminal_view(Rml::ElementDocument* doc, ClientApplicationState& state);

void refresh_bottom_dock_view(Rml::ElementDocument* doc, ClientApplicationState& state);

void refresh_output_view(Rml::ElementDocument* doc, ClientApplicationState& state);

void toggle_terminal(Rml::ElementDocument* doc, ClientApplicationState& state, bool visible);

void toggle_output_panel(Rml::ElementDocument* doc, ClientApplicationState& state, bool visible);

bool complete_terminal_command(Rml::ElementDocument* doc, ClientApplicationState& state);

void sync_shell_visibility(Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientApplicationState& state);

} // namespace nw::toolset::client_application_detail
