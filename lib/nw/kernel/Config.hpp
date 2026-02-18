#pragma once

#include "../util/game_install.hpp"

#include <string>

namespace nw {

/// Configuration options, maybe there will be an actual config file.. someday.
struct ConfigOptions {
    bool include_install = true; ///< Load Game install files
    bool include_user = true;    ///< Load User files, note: if false, value overrides ``include_nwsync``
    std::string profile = "nwn1";
    std::string combat_policy_module = "nwn1.combat";
    std::string init_module = "nwn1.init";
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

    /// Gets configured combat policy module path
    const std::string& combat_policy_module() const noexcept;

    /// Gets configured init module path
    const std::string& init_module() const noexcept;

    /// Gets configured game profile id
    const std::string& profile() const noexcept;

    /// Sets combat policy module path
    void set_combat_policy_module(std::string module);

    /// Sets init module path
    void set_init_module(std::string module);

    /// Sets game profile id
    void set_profile(std::string profile);

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
