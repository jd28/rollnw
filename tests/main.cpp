#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

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
    std::filesystem::create_directory("tmp");
    bool probe = false;
    const char* var = nowide::getenv("ROLLNW_TEST_PROBE_INSTALL");
    if (var) {
        if (auto val = nw::string::from<bool>(var)) {
            probe = *val;
        }
    }

    nw::init_logger(argc, argv);

    if (probe) {
        auto info = nw::probe_nwn_install();
        nw::kernel::config().initialize({
            info.version,
            info.install,
            info.user,
        });
    } else if (nowide::getenv("CI_GITHUB_ACTIONS")) {
        nw::kernel::config().initialize({
            nw::GameVersion::vEE,
            nowide::getenv("NWN_ROOT"),
            "test_data/user/",
        });
    } else {
        nw::kernel::config().initialize({
            nw::GameVersion::vEE,
            "test_data/root/",
            "test_data/user/",
        });
    }

    nw::kernel::services().start();
    nw::kernel::load_profile(new nwn1::Profile);

    Catch::Session session; // There must be exactly one instance

    // writing to session.configData() here sets defaults
    // this is the preferred way to set them

    int returnCode = session.applyCommandLine(argc, argv);
    if (returnCode != 0) // Indicates a command line error
        return returnCode;

    // writing to session.configData() or session.Config() here
    // overrides command line args
    // only do this if you know you need to

    int numFailed = session.run();

    // Not necessary, but best to make sure it doesn't fault.
    nw::kernel::services().shutdown();
    return numFailed;
}
