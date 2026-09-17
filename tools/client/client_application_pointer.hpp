#pragma once
#include "client_application_input.hpp"

namespace nw::toolset::client_application_detail {
ClientEventFlow process_client_pointer_down(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state);
ClientEventFlow process_client_pointer_motion(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state);
ClientEventFlow process_client_pointer_wheel(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state);
} // namespace nw::toolset::client_application_detail
