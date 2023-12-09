#pragma once

#include "../formats/Ini.hpp"
#include "../util/game_install.hpp"
#include "toml++/toml.hpp"

namespace nw {

/// Configuration options, maybe there will be an actual config file.. someday.
struct ConfigOptions {
    bool include_install = true; ///< Load Game install files
    bool include_nwsync = true;  ///< Load NWSync files
    bool include_user = true;    ///< Load User files, note: if false, value overrides ``include_nwsync``
};

namespace kernel {

struct Config {
    explicit Config() = default;

    /// Gets the path of an alias
    std::filesystem::path alias_path(PathAlias alias);

    /// Initializes configuration system
    void initialize(ConfigOptions options = {});

    /// Game installation path
    const std::filesystem::path& install_path() const noexcept;

    /// Gets nwn.ini
    const Ini& nwn_ini() const noexcept;

    /// Gets nwnplayer.ini
    const Ini& nwnplayer_ini() const noexcept;

    /// Gets installation info
    const ConfigOptions& options() const noexcept;

    /// Resolves a path alias
    std::filesystem::path resolve_alias(std::string_view alias_path) const;

    /// Sets game paths.
    /// @note If paths are unset, the kernel will attempt to find them.
    void set_paths(const std::filesystem::path install, const std::filesystem::path user);

    /// Sets game version
    void set_version(GameVersion version);

    /// Gets settings.tml
    /// @note Return value will be `empty` if 1.69
    const toml::table& settings_tml() const noexcept;

    /// Path to user directory
    const std::filesystem::path& user_path() const noexcept;

    /// Gets userpatch.ini or nwnpatch.ini if 1.69
    const Ini& userpatch_ini() const noexcept;

    /// Gets games version
    GameVersion version() const noexcept;

private:
    GameVersion version_ = GameVersion::vEE; ///< Game version
    std::filesystem::path install_;          ///< Path to Game installation directory
    std::filesystem::path user_;             ///< Path to User directory

    ConfigOptions options_;
    Ini nwn_ini_;
    Ini nwnplayer_ini_;
    Ini userpatch_ini_;
    toml::table settings_tml_;
};

} // namespace kernel
} // namespace nw
