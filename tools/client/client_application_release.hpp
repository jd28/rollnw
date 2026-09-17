#pragma once
#include "client_application_input.hpp"

namespace nw::toolset::client_application_detail {
ClientEventFlow process_client_pointer_up(SDL_Event& event, ClientInputDispatchState& dispatch, ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state);
} // namespace nw::toolset::client_application_detail
