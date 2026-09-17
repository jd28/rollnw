#pragma once

#include <filesystem>

namespace nw::toolset {

// Process bootstrap singletons shared by CLI and desktop. No window/video is
// initialized by these functions. Kernel service lifetime belongs to the caller.
[[nodiscard]] std::filesystem::path client_base_path();
void start_client_kernel(const std::filesystem::path& install,
    const std::filesystem::path& user);

} // namespace nw::toolset
