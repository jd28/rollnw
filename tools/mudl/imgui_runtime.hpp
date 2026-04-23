#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

namespace nw::gfx {
struct CommandList;
}

namespace mudl {

struct AppState;

bool init_imgui(AppState& state, SDL_Window* window);
void shutdown_imgui();
void imgui_process_event(const SDL_Event* event);
bool imgui_wants_keyboard();
bool imgui_wants_mouse();
void imgui_prepare_frame(AppState& state, nw::gfx::CommandList* cmd);
void imgui_render(AppState& state, nw::gfx::CommandList* cmd);

} // namespace mudl
