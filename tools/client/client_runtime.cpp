#include "client_runtime.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/runtime.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

namespace nw::toolset {
namespace {

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return false;
    }

    std::string normalized{value};
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized != "0" && normalized != "false" && normalized != "off" && normalized != "no";
}

bool client_ui_dir_exists(const std::filesystem::path& path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::is_directory(path, ec)
        && fs::exists(path / "package.json", ec)
        && fs::exists(path / "panel.rml", ec)
        && fs::exists(path / "panel.rcss", ec);
}

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

std::pair<int, int> query_window_pixels(SDL_Window* window)
{
    if (!window) { return {0, 0}; }
    int pixel_w = 0;
    int pixel_h = 0;
    SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
    if (pixel_w <= 0 || pixel_h <= 0) {
        SDL_GetWindowSize(window, &pixel_w, &pixel_h);
    }
    return {pixel_w, pixel_h};
}

std::pair<int, int> query_window_size(SDL_Window* window)
{
    if (!window) { return {0, 0}; }
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    return {window_w, window_h};
}

void log_window_metrics(SDL_Window* window, const char* label)
{
    if (!window) {
        return;
    }

    const auto window_size = query_window_size(window);
    const auto pixel_size = query_window_pixels(window);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
        "rollnw client window metrics [%s]: window=%dx%d pixels=%dx%d display_scale=%.3f pixel_density=%.3f flags=0x%llx",
        label,
        window_size.first,
        window_size.second,
        pixel_size.first,
        pixel_size.second,
        static_cast<double>(SDL_GetWindowDisplayScale(window)),
        static_cast<double>(SDL_GetWindowPixelDensity(window)),
        static_cast<unsigned long long>(SDL_GetWindowFlags(window)));
}

std::filesystem::path resolve_client_ui_dir()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path base_path = client_base_path();
    const fs::path cwd = fs::current_path(ec);
    const fs::path source_dir = fs::path{__FILE__}.parent_path();
    const std::array<fs::path, 4> candidates{
        base_path / "ui",
        cwd / "ui",
        cwd / "tools/client/ui",
        source_dir / "ui",
    };

    for (const fs::path& candidate : candidates) {
        if (client_ui_dir_exists(candidate)) {
            return fs::weakly_canonical(candidate, ec);
        }
    }

    return {};
}

bool client_frame_pacing_enabled()
{
    return !environment_flag_enabled("ROLLNW_CLIENT_UNCAPPED");
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
