#include "Config.hpp"

#include "../log.hpp"
#include "../util/platform.hpp"
#include "../util/string.hpp"

namespace fs = std::filesystem;

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

    if (version_ == GameVersion::vEE) {
        auto path = user_ / "nwn.ini";
        if (fs::exists(path)) { nwn_ini_ = Ini{path}; }

        path = user_ / "nwnplayer.ini";
        if (fs::exists(path)) { nwnplayer_ini_ = Ini{path}; }

        path = user_ / "userpatch.ini";
        if (fs::exists(path)) { userpatch_ini_ = Ini{path}; }

        path = user_ / "settings.tml";
        if (fs::exists(path)) {
            settings_tml_ = toml::parse_file(path_to_string(path));
        }
    } else if (version_ == GameVersion::v1_69) {
        auto path = user_ / "nwn.ini";
        if (fs::exists(path)) { nwn_ini_ = Ini{path}; }

        path = user_ / "nwnplayer.ini";
        if (fs::exists(path)) { nwnplayer_ini_ = Ini{path}; }

        path = user_ / "nwnpatch.ini";
        if (fs::exists(path)) { userpatch_ini_ = Ini{path}; }
    }
}

const std::filesystem::path& Config::install_path() const noexcept
{
    return install_;
}

const ConfigOptions& Config::options() const noexcept
{
    return options_;
}

const Ini& Config::nwn_ini() const noexcept
{
    return nwn_ini_;
}

const Ini& Config::nwnplayer_ini() const noexcept
{
    return nwnplayer_ini_;
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

const toml::table& Config::settings_tml() const noexcept
{
    return settings_tml_;
}

const std::filesystem::path& Config::user_path() const noexcept
{
    return user_;
}

const Ini& Config::userpatch_ini() const noexcept
{
    return userpatch_ini_;
}

GameVersion Config::version() const noexcept
{
    return version_;
}

} // namespace nw::kernel
