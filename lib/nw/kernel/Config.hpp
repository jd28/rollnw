#pragma once

#include "../formats/Ini.hpp"
#include "../util/game_install.hpp"
#include "toml++/toml.hpp"

namespace nw {

namespace kernel {

struct Config {
    explicit Config(InstallInfo info);

    /// Gets the path of an alias
    std::filesystem::path alias_path(PathAlias alias);

    /// Gets installation info
    const InstallInfo& install_info() const noexcept;

    /// Gets nwn.ini
    const Ini& nwn_ini() const noexcept;

    /// Gets nwnplayer.ini
    const Ini& nwnplayer_ini() const noexcept;

    /// Resolves a path alias
    std::filesystem::path resolve_alias(std::string_view alias_path) const;

    /// Gets settings.tml
    /// @note Return value will be `empty` if 1.69
    const toml::table& settings_tml() const noexcept;

    /// Gets userpatch.ini or nwnpatch.ini if 1.69
    const Ini& userpatch_ini() const noexcept;

private:
    InstallInfo install_info_;
    // Config
    Ini nwn_ini_;
    Ini nwnplayer_ini_;
    Ini userpatch_ini_;
    toml::table settings_tml_;
};

} // namespace kernel
} // namespace nw
