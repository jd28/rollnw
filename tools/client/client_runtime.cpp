#include "client_runtime.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <SDL3/SDL.h>

#include <system_error>
#include <utility>

namespace nw::toolset {
namespace {

void register_smalls_packages()
{
    const auto stdlib_path = client_base_path() / "stdlib";
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path(stdlib_path / "core");
    runtime.add_module_path(stdlib_path / *nw::kernel::config().profile());
}

} // namespace

std::filesystem::path client_base_path()
{
    if (const char* base_path = SDL_GetBasePath(); base_path && base_path[0] != '\0') {
        return std::filesystem::path{base_path};
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

void start_client_kernel(const std::filesystem::path& install, const std::filesystem::path& user)
{
    nw::kernel::config().set_paths(install, user);
    nw::ConfigOptions options;
    options.profile = "nwn1";
    options.init_module = "";
    nw::kernel::config().initialize(std::move(options));
    nw::kernel::config().set_init_module("");
    nw::kernel::services().create();
    register_smalls_packages();
    nw::kernel::services().start();
}

} // namespace nw::toolset
