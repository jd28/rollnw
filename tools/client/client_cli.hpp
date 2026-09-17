#pragma once

namespace nw::toolset {

// Returns -1 for desktop startup, otherwise the existing CLI exit status.
// Arguments are borrowed only for this call. No SDL video/window initialization.
[[nodiscard]] int run_project_cli_if_requested(int argc, char* argv[]);
[[nodiscard]] bool print_client_build_info_if_requested(int argc, char* argv[]);

} // namespace nw::toolset
