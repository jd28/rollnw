#include "server.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <iostream>
#include <string>

namespace lsp = nw::smalls;

int main(int argc, char* argv[])
{
    loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
    nw::init_logger(argc, argv);

    nw::kernel::config().initialize();
    nw::kernel::services().start();

    auto& rt = nw::kernel::runtime();
    rt.set_diagnostic_config({lsp::DebugLevel::full});
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-I" || arg == "--module-path") && i + 1 < argc) {
            rt.add_module_path(argv[++i]);
        }
    }

    run_smalls_lsp(std::cin, std::cout);

    nw::kernel::services().shutdown();
    return 0;
}
