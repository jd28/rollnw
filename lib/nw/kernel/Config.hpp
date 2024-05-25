#pragma once

#include "../util/game_install.hpp"

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

    /// Initializes configuration system
    void initialize(ConfigOptions options = {});

    /// Game installation path
    const std::filesystem::path& install_path() const noexcept;

    /// Max resref length
    size_t max_resref_length() const;

    /// Gets installation info
    const ConfigOptions& options() const noexcept;

    /// Sets game paths.
    /// @note If paths are unset, the kernel will attempt to find them.
    void set_paths(const std::filesystem::path install, const std::filesystem::path user);

    /// Sets game version
    void set_version(GameVersion version);

    /// Path to user directory
    const std::filesystem::path& user_path() const noexcept;

    /// Gets games version
    GameVersion version() const noexcept;

private:
    GameVersion version_ = GameVersion::vEE; ///< Game version
    std::filesystem::path install_;          ///< Path to Game installation directory
    std::filesystem::path user_;             ///< Path to User directory

    ConfigOptions options_;
};

} // namespace kernel
} // namespace nw
