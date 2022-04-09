#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>

int main(int argc, char* argv[])
{
    std::filesystem::create_directory("tmp");

    nw::init_logger(argc, argv);

    nw::kernel::config().initialize({
        nw::GameVersion::vEE,
        "test_data/root/",
        "test_data/user/",
    });
    nw::kernel::services().start();

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
