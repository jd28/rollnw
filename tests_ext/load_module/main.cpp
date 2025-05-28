#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Module.hpp>

int main(int argc, char* argv[])
{
    if (argc <= 1) { return 1; }

    nw::init_logger(argc, argv);
    nw::kernel::config().initialize();

    auto start = std::chrono::high_resolution_clock::now();

    auto mod = nw::kernel::load_module(argv[1]);
    if (!mod) { return 1; }

    auto stop = std::chrono::high_resolution_clock::now();
    auto total = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    LOG_F(INFO, "module loaded {} areas in {}ms", mod->area_count(), total);
    return 0;
}
