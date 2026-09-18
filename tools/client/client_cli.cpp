#include "client_cli.hpp"
#include "blueprint_operations.hpp"
#include "client_runtime.hpp"
#include "project.hpp"
#include "rollnw_tool_version.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/util/game_install.hpp>

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace nw::toolset {
namespace {

void print_cli_usage(std::ostream& out)
{
    out << "Usage:\n"
        << "  rollnw-client --version\n"
        << "  rollnw-client --build-info\n"
        << "  rollnw-client init <project-dir>\n"
        << "  rollnw-client import (--json|--legacy) <module.mod> [project-dir]\n";
}

std::filesystem::path default_import_project_dir(const std::filesystem::path& module_path)
{
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    const auto base = cwd.empty() ? std::filesystem::path{"."} : cwd;
    return base / module_path.stem();
}

int run_project_init_cli(int argc, char* argv[])
{
    if (argc != 3) {
        print_cli_usage(std::cerr);
        return 2;
    }

    const auto result = nw::toolset::initialize_project(std::filesystem::path{argv[2]});
    (result.ok ? std::cout : std::cerr) << result.message << '\n';
    return result.ok ? 0 : 1;
}

bool ensure_project_import_kernel(nw::toolset::ProjectImportFormat format, std::ostream& err)
{
    if (format != nw::toolset::ProjectImportFormat::json) {
        return true;
    }

    if (nw::kernel::services().get<nw::kernel::Rules>()) {
        return true;
    }

    const auto install = nw::probe_nwn_install(nw::GameVersion::vEE);
    if (install.install.empty()) {
        err << "rollnw-client: failed to find NWN install; set NWN_ROOT and NWN_HOME\n";
        return false;
    }

    try {
        start_client_kernel(install.install, install.user);
    } catch (const std::exception& e) {
        err << "rollnw-client: failed to initialize import services: " << e.what() << '\n';
        return false;
    }

    return true;
}

int run_project_import_cli(int argc, char* argv[])
{
    nw::toolset::ProjectImportOptions options;
    bool json = false;
    bool legacy = false;
    std::vector<std::string_view> positional;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--json") {
            json = true;
            options.format = nw::toolset::ProjectImportFormat::json;
        } else if (arg == "--legacy") {
            legacy = true;
            options.format = nw::toolset::ProjectImportFormat::legacy;
        } else if (!arg.empty() && arg.front() == '-') {
            print_cli_usage(std::cerr);
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (json == legacy || positional.empty() || positional.size() > 2) {
        print_cli_usage(std::cerr);
        return 2;
    }

    const std::filesystem::path module_path{positional[0]};
    const std::filesystem::path project_dir = positional.size() == 2
        ? std::filesystem::path{positional[1]}
        : default_import_project_dir(module_path);

    if (!ensure_project_import_kernel(options.format, std::cerr)) {
        return 1;
    }

    try {
        const auto result = nw::toolset::import_module_project(module_path, project_dir, options);
        (result.ok ? std::cout : std::cerr) << result.message << '\n';
        return result.ok ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "rollnw-client: failed to import module: " << error.what() << '\n';
        return 1;
    }
}

} // namespace

int run_project_cli_if_requested(int argc, char* argv[])
{
    if (argc <= 1) {
        return -1;
    }

    const std::string_view command{argv[1]};
    if (command == "blueprint-update") {
        if (argc != 4) { return 2; }
        try {
            const std::filesystem::path operation{argv[2]};
            const std::string_view phase{argv[3]};
            if (phase != "restore") {
                std::ifstream input{operation / "request.json"};
                const auto request = nlohmann::json::parse(input);
                if (request.at("version") != 1 || request.at("profile") != "nwn1") {
                    std::cerr << "Unsupported blueprint worker configuration\n";
                    return 1;
                }
                start_client_kernel(request.at("install").get<std::string>(), request.at("user").get<std::string>());
                const std::filesystem::path project{request.at("project").get<std::string>()};
                const auto options = nw::kernel::module_load_options_for_project(project);
                if (!nw::kernel::load_module(project, false, options)) {
                    std::cerr << "Cannot load blueprint operation project\n";
                    return 1;
                }
            }
            std::string error;
            if (!nw::toolset::run_blueprint_update_operation(operation, phase, error)) {
                std::cerr << error << '\n';
                return 1;
            }
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << '\n';
            return 1;
        }
    }
    if (command == "init") {
        return run_project_init_cli(argc, argv);
    }
    if (command == "import") {
        return run_project_import_cli(argc, argv);
    }
    if (command == "--help" || command == "-h" || command == "help") {
        print_cli_usage(std::cout);
        return 0;
    }
    return -1;
}

bool print_client_build_info_if_requested(int argc, char* argv[])
{
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << ROLLNW_TOOL_NAME " " ROLLNW_TOOL_VERSION "\n";
        return true;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--build-info") {
        std::cout << ROLLNW_TOOL_BUILD_INFO "\n";
        return true;
    }
    return false;
}

} // namespace nw::toolset
