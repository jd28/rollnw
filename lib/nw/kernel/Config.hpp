#pragma once

#include "../formats/Ini.hpp"
#include "../util/game_install.hpp"
#include "toml++/toml.hpp"

namespace nw {

enum struct PathAlias {
    ambient,
    cache,
    currentgame,
    database,
    development,
    dmvault,
    hak,
    hd0,
    localvault,
    logs,
    modelcompiler,
    modules,
    movies,
    music,
    nwsync,
    oldservervault,
    override,
    patch,
    portraits,
    saves,
    screenshots,
    servervault,
    temp,
    tempclient,
    tlk,
    user = hd0,
};

namespace kernel {

struct Config {
    Config() = default;

    /// Gets the path of an alias
    std::filesystem::path alias_path(PathAlias alias);

    /// Loads configuration files
    void load(const InstallInfo& info);

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
    // Config
    Ini nwn_ini_;
    Ini nwnplayer_ini_;
    Ini userpatch_ini_;
    toml::table settings_tml_;
};

} // namespace kernel
} // namespace nw
