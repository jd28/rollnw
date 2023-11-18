#pragma once

#include "../formats/Ini.hpp"
#include "../util/game_install.hpp"
#include "toml++/toml.hpp"

namespace nw {

/// Configuration options, maybe there will be an actual config file.. someday.
struct ConfigOptions {
    GameVersion version;           ///< Game version
    std::filesystem::path install; ///< Path to Game installation directory
    std::filesystem::path user;    ///< Path to User directory
    bool include_install = true;   ///< Load Game install files
    bool include_nwsync = true;    ///< Load NWSync files
    bool include_user = true;      ///< Load User files, note: if false, value overrides ``include_nwsync``
};

namespace kernel {

struct Config {
    explicit Config() = default;

    /// Gets the path of an alias
    std::filesystem::path alias_path(PathAlias alias);

    /// Initializes configuration system
    void initialize(ConfigOptions options);

    /// Gets nwn.ini
    const Ini& nwn_ini() const noexcept;

    /// Gets nwnplayer.ini
    const Ini& nwnplayer_ini() const noexcept;

    /// Gets installation info
    const ConfigOptions& options() const noexcept;

    /// Resolves a path alias
    std::filesystem::path resolve_alias(std::string_view alias_path) const;

    /// Gets settings.tml
    /// @note Return value will be `empty` if 1.69
    const toml::table& settings_tml() const noexcept;

    /// Gets userpatch.ini or nwnpatch.ini if 1.69
    const Ini& userpatch_ini() const noexcept;

private:
    ConfigOptions options_;
    Ini nwn_ini_;
    Ini nwnplayer_ini_;
    Ini userpatch_ini_;
    toml::table settings_tml_;
};

} // namespace kernel
} // namespace nw
