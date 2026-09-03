#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/core.h>
#include <nowide/cstdlib.hpp>

#include <algorithm>
#include <filesystem>
#include <vector>

#include "../../tests/test_nwn_root.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    fs::path scripts_dir;
    if (argc > 1) {
        scripts_dir = argv[1];
    } else {
        const fs::path repo_root = fs::path(__FILE__).parent_path().parent_path().parent_path();
        scripts_dir = repo_root / "lib" / "nw" / "smalls" / "scripts";
    }

    if (!fs::exists(scripts_dir)) {
        fmt::print(stderr, "Scripts directory not found: {}\n", scripts_dir.string());
        return 1;
    }

    if (!nw::test::configure_dedicated_server("test_data/user/")) { return 1; }
    nw::ConfigOptions config_options;
    config_options.profile = "nwn1";
    nw::kernel::config().initialize(std::move(config_options));
    nw::kernel::services().create();
    nw::kernel::runtime().add_module_path(scripts_dir / "core");
    nw::kernel::runtime().add_module_path(scripts_dir / "nwn1");
    nw::kernel::services().start();

    auto& rt = nw::kernel::runtime();
    rt.add_module_path(scripts_dir / "tests");
    rt.set_script_tests_enabled(true);

    int total_scripts = 0;
    int successful_scripts = 0;
    int failed_scripts = 0;
    int total_failures = 0;

    std::vector<fs::path> test_scripts;
    for (const auto& entry : fs::directory_iterator(scripts_dir / "tests")) {
        if (!entry.is_regular_file()) { continue; }
        if (entry.path().extension() != ".smalls") { continue; }
        test_scripts.push_back(entry.path());
    }
    std::ranges::sort(test_scripts);

    for (const auto& path : test_scripts) {
        const std::string module_name = "tests." + path.stem().string();

        fmt::print("Running: {}\n", module_name);
        total_scripts++;

        auto* script = rt.load_module(module_name);
        if (!script) {
            fmt::print(stderr, "  Failed to load module: {}\n", module_name);
            failed_scripts++;
            continue;
        }

        if (script->errors() > 0) {
            fmt::print(stderr, "  Module has {} errors\n", script->errors());
            failed_scripts++;
            continue;
        }

        const auto tests = rt.module_tests(script);
        if (tests.empty()) {
            fmt::print(stderr, "  Module has no script tests\n");
            failed_scripts++;
            continue;
        }

        rt.reset_test_state();
        const auto results = rt.execute_tests(script);
        bool executed = results.size() == tests.size();
        for (const auto& result : results) {
            if (!result.ok()) {
                fmt::print(stderr, "  Execution failed: {}\n", result.error_message);
                executed = false;
            }
        }

        if (!executed || rt.test_failures() > 0) {
            failed_scripts++;
            total_failures += static_cast<int>(rt.test_failures());
        } else {
            successful_scripts++;
        }
    }

    fmt::print("\n");
    fmt::print("=== Smalls Test Summary ===\n");
    fmt::print("Scripts: {} passed, {} failed, {} total\n", successful_scripts, failed_scripts, total_scripts);
    fmt::print("Test failures: {}\n", total_failures);

    nw::kernel::services().shutdown();

    return failed_scripts > 0 ? 1 : 0;
}
