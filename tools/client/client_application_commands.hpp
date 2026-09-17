#pragma once

#include "client_application_state.hpp"
#include "renderer.hpp"
#include <RmlUi/Core/EventListener.h>

namespace nw::toolset::client_application_detail {

// Private root command/loading singleton coordination. Existing owning results/
// args and borrowed SDK surfaces are consumed synchronously; owners remain root.
class BlueprintActionListener final : public Rml::EventListener {
public:
    BlueprintActionListener(SDL_Window* window, Rml::ElementDocument* document, ClientApplicationState& state);
    void ProcessEvent(Rml::Event&) override;

private:
    SDL_Window* window_;
    Rml::ElementDocument* document_;
    ClientApplicationState& state_;
};

class HomeProjectActionListener final : public Rml::EventListener {
public:
    HomeProjectActionListener(SDL_Window* window, Rml::ElementDocument* document, ClientApplicationState& state);
    void ProcessEvent(Rml::Event&) override;

private:
    SDL_Window* window_;
    Rml::ElementDocument* document_;
    ClientApplicationState& state_;
};

void sync_command_overlay_visibility(ClientApplicationState& state);
void close_command_form_combobox(ClientApplicationState& state);
void sync_command_form(ClientApplicationState& state, bool force = false);
void sync_blueprint_operation(ClientApplicationState& state);
void sync_project_load_overlay(ClientApplicationState& state);
bool queue_project_open(ClientApplicationState& state, std::string path,
    nw::toolset::CommandSource source, bool close_import_panel_on_success = false);
nw::toolset::CommandResult resolve_command_result(SDL_Window* window,
    ClientApplicationState& state,
    nw::toolset::CommandResult result,
    nw::toolset::CommandSource source,
    bool terminal_output = false);
nw::toolset::CommandResult dispatch_command_flow(SDL_Window* window,
    ClientApplicationState& state,
    std::string_view command_id,
    std::vector<std::string_view> args,
    nw::toolset::CommandSource source);
void show_open_module_dialog(SDL_Window* window, ClientApplicationState& state, bool import = false);
void show_open_project_dialog(SDL_Window* window, ClientApplicationState& state, bool import = false);
void execute_palette_command(SDL_Window* window,
    Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientApplicationState& state,
    std::string_view command_id);
void handle_open_module_dialog_result(SDL_Window* window, Rml::ElementDocument* doc, ClientApplicationState& state, SDL_Event& event);
void run_command_form_action(SDL_Window* window, Rml::ElementDocument* doc, ClientApplicationState& state, size_t index);
void poll_project_import(SDL_Window* window, Rml::ElementDocument* doc, ClientApplicationState& state);
void poll_project_open(SDL_Window* window,
    Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientRenderer& renderer,
    ClientApplicationState& state);

void toggle_command_palette(Rml::Context* context,
    Rml::Context* palette_context,
    Rml::ElementDocument* doc,
    Rml::ElementDocument* palette_doc,
    ClientApplicationState& state,
    bool visible);

nw::toolset::CommandContext command_context(ClientApplicationState& state, nw::toolset::CommandSource source);

void append_command_result(ClientApplicationState& state, const nw::toolset::CommandResult& result);

void append_terminal_result(ClientApplicationState& state, const nw::toolset::CommandResult& result);

nw::toolset::CommandResult dispatch_command(ClientApplicationState& state,
    std::string_view command_id,
    std::vector<std::string_view> args,
    nw::toolset::CommandSource source);

bool ensure_backend_ready(ClientApplicationState& state);

} // namespace nw::toolset::client_application_detail
