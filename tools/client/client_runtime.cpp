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

ClientKernelRuntime::ClientKernelRuntime(const std::filesystem::path& install, const std::filesystem::path& user)
{
    try {
        start_client_kernel(install, user);
    } catch (...) {
        nw::kernel::services().shutdown();
        throw;
    }
}

ClientKernelRuntime::~ClientKernelRuntime()
{
    nw::kernel::services().shutdown();
}

ClientSdlRuntime::~ClientSdlRuntime()
{
    shutdown();
}

bool ClientSdlRuntime::initialize_video(const char* version, const char* identifier)
{
    if (initialization_attempted_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Repeated SDL initialization");
        return false;
    }
    initialization_attempted_ = true;
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    if (!SDL_SetAppMetadata("rollnw | client", version, identifier)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetAppMetadata failed: %s", SDL_GetError());
    }
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "application");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool ClientSdlRuntime::create_window()
{
    if (!initialization_attempted_ || window_ || !SDL_WasInit(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window creation requires owned video and no existing window");
        return false;
    }
    window_ = SDL_CreateWindow("rollnw | client", 1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    if (!window_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    SDL_ShowWindow(window_);
    return true;
}

void ClientSdlRuntime::shutdown()
{
    if (initialization_attempted_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        initialization_attempted_ = false;
    }
}

} // namespace nw::toolset
