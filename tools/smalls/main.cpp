#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/core.h>
#include <nowide/args.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void print_usage(const char* program)
{
    fmt::print("Usage: {} <command> [options] <module>\n\n", program);
    fmt::print("Commands:\n");
    fmt::print("  check <module>   Load, resolve, and compile module\n");
    fmt::print("  run <module>     Load, resolve, compile, and execute main()\n\n");
    fmt::print("Arguments:\n");
    fmt::print("  <module>             Module name (e.g., arithmetic)\n\n");
    fmt::print("Options:\n");
    fmt::print("  --scripts <dir>      Add all subdirectories of <dir> to module path\n");
    fmt::print("  -I, --module-path <dir>  Add <dir> to module path (repeatable)\n");
    fmt::print("  --gas <n>            Set gas limit for execution (default: 100000)\n");
}

int main(int argc, char* argv[])
{
    nowide::args _(argc, argv);
    nw::init_logger(argc, argv);

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command;
    std::string module_name;
    std::vector<fs::path> module_paths;
    uint64_t gas_limit = nw::smalls::Runtime::default_gas_limit;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--scripts" && i + 1 < argc) {
            fs::path scripts_dir = argv[++i];
            for (const auto& entry : fs::directory_iterator(scripts_dir)) {
                if (entry.is_directory()) {
                    module_paths.push_back(entry.path());
                }
            }
        } else if ((arg == "-I" || arg == "--module-path") && i + 1 < argc) {
            module_paths.push_back(argv[++i]);
        } else if (arg == "--gas" && i + 1 < argc) {
            gas_limit = std::stoull(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (command.empty()) {
            command = arg;
        } else if (module_name.empty()) {
            module_name = arg;
        }
    }

    if (command.empty() || module_name.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    if (command != "check" && command != "run") {
        fmt::print(stderr, "Unknown command: {}\n", command);
        return 1;
    }

    nw::kernel::config().initialize();
    nw::kernel::services().start();

    auto& rt = nw::kernel::runtime();

    for (const auto& path : module_paths) {
        if (fs::exists(path)) {
            rt.add_module_path(path);
        } else {
            fmt::print(stderr, "Warning: module path does not exist: {}\n", path.string());
        }
    }

    auto* script = rt.load_module(module_name);
    if (!script) {
        fmt::print(stderr, "Failed to load module: {}\n", module_name);
        nw::kernel::services().shutdown();
        return 1;
    }

    for (const auto& diag : script->diagnostics()) {
        fmt::print(stderr, "{}\n", diag.message);
    }

    if (script->errors() > 0) {
        fmt::print(stderr, "Module has {} error(s)\n", script->errors());
        nw::kernel::services().shutdown();
        return 1;
    }

    auto* module = rt.get_or_compile_module(script);
    if (!module) {
        fmt::print(stderr, "Failed to compile module: {}\n", module_name);
        nw::kernel::services().shutdown();
        return 1;
    }

    if (command == "check") {
        fmt::print("Module '{}' compiled successfully\n", module_name);
        nw::kernel::services().shutdown();
        return 0;
    }

    if (!script->exports().find("main")) {
        fmt::print(stderr, "Module has no exported 'main' function\n");
        nw::kernel::services().shutdown();
        return 1;
    }

    auto result = rt.execute_script(script, "main", {}, gas_limit);
    if (!result.ok()) {
        fmt::print(stderr, "Execution failed: {}\n", result.error_message);
        if (!result.stack_trace.empty()) {
            fmt::print(stderr, "{}\n", result.stack_trace);
        }
        nw::kernel::services().shutdown();
        return 1;
    }

    nw::kernel::services().shutdown();
    return 0;
}
