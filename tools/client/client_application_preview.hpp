#pragma once
#include "client_application_state.hpp"
#include "renderer.hpp"
#include <RmlUi_Platform_SDL.h>

namespace nw::toolset::client_application_detail {

// Private composition functions. Feature APIs receive only their own states.
void restore_play_preview_picker_shell(Rml::ElementDocument* doc, ClientApplicationState& state);

void stop_play_preview(ClientRenderer& renderer, SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc, ClientApplicationState& state);

void request_play_preview_actor(Rml::ElementDocument* doc, ClientApplicationState& state, std::string_view reason);

bool start_play_preview_from_ray(ClientRenderer& renderer, SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc, ClientApplicationState& state, const ClientViewportRay& ray);

bool prepare_play_preview(ClientRenderer&, SystemInterface_SDL&, Rml::ElementDocument*, ClientApplicationState&, const std::filesystem::path& selected_actor = {});

void sync_viewer_render_options(ClientRenderer& renderer, const ClientApplicationState& state);

} // namespace nw::toolset::client_application_detail
