#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Module.hpp>

int main(int argc, char* argv[])
{
    if (argc <= 1) { return 1; }

    nw::init_logger(argc, argv);
    nw::kernel::config().initialize();

    auto mod = nw::kernel::load_module(argv[1]);
    if (!mod) { return 1; }

    return 0;
}
