#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Module.hpp>

#include <nlohmann/json.hpp>

#include "../../tests/test_nwn_root.hpp"

int main(int argc, char* argv[])
{
    if (argc <= 1) { return 1; }

    nw::init_logger(argc, argv);
    if (!nw::test::configure_dedicated_server("test_data/user/")) { return 1; }
    nw::kernel::config().initialize();

    auto mod = nw::kernel::load_module(argv[1]);
    if (!mod) { return 1; }

    auto j = nw::kernel::stats();
    LOG_F(INFO, "\n{}", j.dump(2));

    return 0;
}
