#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/util/string.hpp>

#include "test_nwn_root.hpp"

namespace {

// Fixtures may replace or stop the process-owned services to obtain a clean
// runtime. Restore game-mode services after fixture teardown so shard ordering
// cannot affect the next test. start() is a no-op on the common path.
class RestoreGameServices final : public ::testing::EmptyTestEventListener {
    void OnTestEnd(const ::testing::TestInfo&) override
    {
        auto& services = nw::kernel::services();
        if (services.mode() != nw::kernel::ServiceMode::game) {
            services.shutdown();
        }
        services.start();
    }
};

} // namespace

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
        nw::ConfigOptions config_options;
        config_options.profile = "nwn1";
        nw::kernel::config().initialize(std::move(config_options));
        nw::kernel::services().start();
    }

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::UnitTest::GetInstance()->listeners().Append(new RestoreGameServices);
    int failed = RUN_ALL_TESTS();

    if (!list_tests) {
        // Not necessary, but best to make sure it doesn't fault.
        nw::kernel::services().shutdown();
    }

    return failed;
}
