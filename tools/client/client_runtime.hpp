#pragma once

#include <filesystem>

struct SDL_Window;

namespace nw::toolset {

// Process bootstrap singletons shared by CLI and desktop. No window/video is
// initialized by these functions. Kernel service lifetime belongs to the caller.
[[nodiscard]] std::filesystem::path client_base_path();
void start_client_kernel(const std::filesystem::path& install,
    const std::filesystem::path& user);

// Process kernel singleton. No other live owner may use these services after
// destruction. Existing bootstrap/Services remain the implementation authority.
class ClientKernelRuntime {
public:
    ClientKernelRuntime(const std::filesystem::path& install, const std::filesystem::path& user);
    ~ClientKernelRuntime();
    ClientKernelRuntime(const ClientKernelRuntime&) = delete;
    ClientKernelRuntime& operator=(const ClientKernelRuntime&) = delete;
    ClientKernelRuntime(ClientKernelRuntime&&) = delete;
    ClientKernelRuntime& operator=(ClientKernelRuntime&&) = delete;
};

// Process SDL/window singleton. Metadata is copied by SDL; window() is a borrow
// until shutdown/destruction. Renderer/Rml/devices must be closed first. SDK opaque
// window pointers cannot be replaced with engine indices in those external APIs.
class ClientSdlRuntime {
public:
    ClientSdlRuntime() = default;
    ~ClientSdlRuntime();
    ClientSdlRuntime(const ClientSdlRuntime&) = delete;
    ClientSdlRuntime& operator=(const ClientSdlRuntime&) = delete;
    ClientSdlRuntime(ClientSdlRuntime&&) = delete;
    ClientSdlRuntime& operator=(ClientSdlRuntime&&) = delete;

    [[nodiscard]] bool initialize_video(const char* version, const char* identifier);
    [[nodiscard]] bool create_window();
    [[nodiscard]] SDL_Window* window() const noexcept { return window_; }
    void shutdown();

private:
    SDL_Window* window_ = nullptr;
    bool initialization_attempted_ = false;
};

} // namespace nw::toolset
