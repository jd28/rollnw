#include "Config.hpp"

#include "../log.hpp"

namespace nw::kernel {

void Config::initialize(ConfigOptions options)
{
    options_ = std::move(options);

    if (install_.empty()) {
        auto info = probe_nwn_install(version_);
        install_ = info.install;
        LOG_F(INFO, "install: {}", install_);
        if (user_.empty()) { user_ = info.user; }
    }

    LOG_F(INFO, "kernel: initializing config system");
    LOG_F(INFO, "kernel: root directory: {}", install_);
    LOG_F(INFO, "kernel: user directory: {}", user_);

    // [TODO] Find a better method of conveying this error
    CHECK_F(!install_.empty(), "Failed to find valid NWN install.");
}

const std::filesystem::path& Config::install_path() const noexcept
{
    return install_;
}

size_t Config::max_resref_length() const
{
    switch (version()) {
    default:
        return 32;
    case GameVersion::v1_69:
    case GameVersion::vEE:
        return 16;
    }
}

const ConfigOptions& Config::options() const noexcept
{
    return options_;
}

void Config::set_paths(const std::filesystem::path install, const std::filesystem::path user)
{
    install_ = std::move(install);
    user_ = std::move(user);
}

void Config::set_version(GameVersion version)
{
    version_ = version;
}
const std::filesystem::path& Config::user_path() const noexcept
{
    return user_;
}

GameVersion Config::version() const noexcept
{
    return version_;
}

} // namespace nw::kernel
