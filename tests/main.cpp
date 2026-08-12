#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/log.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include "test_nwn_root.hpp"

int main(int argc, char* argv[])
{
    bool list_tests = false;
    for (int i = 0; i < argc; ++i) {
        if (nw::string::icmp("--gtest_list_tests", argv[i])) {
            list_tests = true;
        }
    }

    std::filesystem::create_directory("tmp");

    nw::init_logger(argc, argv);

    if (!nw::test::configure_dedicated_server("test_data/user/")) { return 1; }

    if (!list_tests) {
        nw::kernel::config().initialize();
        nw::kernel::services().start();
    }

    ::testing::InitGoogleTest(&argc, argv);
    int failed = RUN_ALL_TESTS();

    if (!list_tests) {
        // Not necessary, but best to make sure it doesn't fault.
        nw::kernel::services().shutdown();
    }

    return failed;
}
