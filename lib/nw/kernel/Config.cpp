#include "Config.hpp"

namespace nw::kernel {

void Config::load(const InstallInfo& info)
{
    if (info.version == InstallVersion::vEE) {
        nwn_ini_ = Ini{info.user / "nwn.ini"};
        nwnplayer_ini_ = Ini{info.user / "nwnplayer.ini"};
        userpatch_ini_ = Ini{info.user / "userpatch.ini"};
        settings_tml_ = toml::parse_file((info.user / "settings.tml").u8string());
    } else if (info.version == InstallVersion::v1_69) {
        nwn_ini_ = Ini{info.install / "nwn.ini"};
        nwnplayer_ini_ = Ini{info.install / "nwnplayer.ini"};
        userpatch_ini_ = Ini{info.install / "nwnpatch.ini"};
    }
}

const Ini& Config::nwn_ini() const noexcept
{
    return nwn_ini_;
}

const Ini& Config::nwnplayer_ini() const noexcept
{
    return nwnplayer_ini_;
}

const Ini& Config::userpatch_ini() const noexcept
{
    return userpatch_ini_;
}

const toml::table& Config::settings_tml() const noexcept
{
    return settings_tml_;
}

} // namespace nw::kernel
