#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/core.h>
#include <nowide/cstdlib.hpp>

#include <filesystem>

namespace fs = std::filesystem;

struct TestState {
    int total = 0;
    int passed = 0;
    int failed = 0;
};

static TestState g_state;

void native_test(const char* name, bool passed)
{
    g_state.total++;
    if (passed) {
        g_state.passed++;
        fmt::print("  PASS: {}\n", name);
    } else {
        g_state.failed++;
        fmt::print(stderr, "  FAIL: {}\n", name);
    }
}

void native_reset()
{
    g_state.total = 0;
    g_state.passed = 0;
    g_state.failed = 0;
}

void native_summary()
{
    fmt::print("  Tests: {} passed, {} failed, {} total\n", g_state.passed, g_state.failed, g_state.total);
}

int32_t native_failures() { return g_state.failed; }
int32_t native_count() { return g_state.total; }

void register_test_module(nw::smalls::Runtime& rt)
{
    rt.module("core.test")
        .function("test", &native_test)
        .function("reset", &native_reset)
        .function("summary", &native_summary)
        .function("failures", &native_failures)
        .function("count", &native_count)
        .finalize();
}

int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);

    nw::kernel::config().initialize();
    nw::kernel::services().start();

    auto& rt = nw::kernel::runtime();
    register_test_module(rt);

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

    rt.add_module_path(scripts_dir / "core");
    rt.add_module_path(scripts_dir / "tests");

    int total_scripts = 0;
    int successful_scripts = 0;
    int failed_scripts = 0;
    int total_failures = 0;

    for (const auto& entry : fs::recursive_directory_iterator(scripts_dir)) {
        if (!entry.is_regular_file()) { continue; }
        if (entry.path().extension() != ".smalls") { continue; }

        auto rel_path = fs::relative(entry.path(), scripts_dir);
        std::string module_name;
        for (const auto& part : rel_path.parent_path()) {
            if (!module_name.empty()) { module_name += "."; }
            module_name += part.string();
        }
        if (!module_name.empty()) { module_name += "."; }
        module_name += rel_path.stem().string();

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

        if (!script->exports().find("main")) {
            fmt::print("  Skipping (no main function)\n");
            total_scripts--;
            continue;
        }

        native_reset();
        auto result = rt.execute_script(script, "main");
        if (!result.ok()) {
            fmt::print(stderr, "  Execution failed: {}\n", result.error_message);
            failed_scripts++;
            continue;
        }

        if (g_state.failed > 0) {
            failed_scripts++;
            total_failures += g_state.failed;
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
