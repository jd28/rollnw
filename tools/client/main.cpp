#include "client_application.hpp"
#include "client_cli.hpp"
#include "nw/log.hpp"
#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    if (nw::toolset::print_client_build_info_if_requested(argc, argv)) { return 0; }
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
    nw::init_logger(argc, argv);
    if (const int cli_result = nw::toolset::run_project_cli_if_requested(argc, argv); cli_result >= 0) { return cli_result; }
    return nw::toolset::run_client_application(argv[0]);
}
