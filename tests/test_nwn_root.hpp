#pragma once

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>

#include <nowide/cstdlib.hpp>

#include <filesystem>
#include <optional>
#include <utility>

namespace nw::test {

// Test process configuration is genuinely singular: every test in the process
// must observe the same dedicated-server resource root.
inline std::optional<std::filesystem::path> dedicated_server_root()
{
    const auto* configured_root = nowide::getenv("NWN_ROOT");
    const std::filesystem::path install_root = configured_root && configured_root[0] != '\0'
        ? std::filesystem::path{configured_root}
        : std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "nwn";
    const auto base_key = install_root / "data/nwn_base.key";
    if (!std::filesystem::is_regular_file(base_key)) {
        LOG_F(ERROR, "Test NWN dedicated-server data is missing: {}", base_key.string());
        return std::nullopt;
    }

    return install_root;
}

inline bool configure_dedicated_server(std::filesystem::path user_root)
{
    auto install_root = dedicated_server_root();
    if (!install_root) { return false; }

    nw::kernel::config().set_paths(*install_root, std::move(user_root));
    return true;
}

} // namespace nw::test
