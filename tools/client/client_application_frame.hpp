#pragma once
#include "client_application_input.hpp"

namespace nw::toolset::client_application_detail {
// One ordered desktop frame stream; no frozen focus state or new action queue.
int run_client_frames(ClientApplicationSurfaces& surfaces, ClientRenderer& renderer, ClientApplicationState& state, ObjectWorkbenchChangeListener& object_workbench_change_listener, LoguruOutputCapture& log_capture);
} // namespace nw::toolset::client_application_detail
