#pragma once

#include "../formats/Ini.hpp"
#include "../util/game_install.hpp"
#include "toml++/toml.hpp"

namespace nw::kernel {

struct Config {
    Config() = default;

    /// Loads configuration files
    void load(const InstallInfo& info);

    /// Gets nwn.ini
    const Ini& nwn_ini() const noexcept;

    /// Gets nwnplayer.ini
    const Ini& nwnplayer_ini() const noexcept;

    /// Gets userpatch.ini or nwnpatch.ini if 1.69
    const Ini& userpatch_ini() const noexcept;

    /// Gets settings.tml
    /// @note Return value will be `empty` if 1.69
    const toml::table& settings_tml() const noexcept;

private:
    // Config
    Ini nwn_ini_;
    Ini nwnplayer_ini_;
    Ini userpatch_ini_;
    toml::table settings_tml_;
};

} // namespace nw::kernel
