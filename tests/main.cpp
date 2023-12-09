#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/log.hpp>
#include <nw/util/string.hpp>
#include <nwn1/Profile.hpp>

#include <nowide/cstdlib.hpp>

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

    if (nowide::getenv("CI_GITHUB_ACTIONS")) {
        nw::kernel::config().set_paths(nowide::getenv("NWN_ROOT"), "test_data/user/");
    } else {
        nw::kernel::config().set_paths("", "test_data/user/");
    }

    if (!list_tests) {
        nw::kernel::config().initialize();
        nw::kernel::services().start();
        nw::kernel::load_profile(new nwn1::Profile);
    }

    ::testing::InitGoogleTest(&argc, argv);
    int failed = RUN_ALL_TESTS();

    if (!list_tests) {
        // Not necessary, but best to make sure it doesn't fault.
        nw::kernel::services().shutdown();
    }

    return failed;
}
