#include "Config.hpp"

#include "../log.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace nw::kernel {

namespace {

bool valid_profile_root(std::string_view value)
{
    if (value.empty() || !((value.front() >= 'a' && value.front() <= 'z') || value.front() == '_')) {
        return false;
    }

    return std::ranges::all_of(value.substr(1), [](char ch) {
        return (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9')
            || ch == '_';
    });
}

} // namespace

void Config::initialize(ConfigOptions options)
{
    if (!valid_profile_root(options.profile)) {
        throw std::invalid_argument("profile root must match [a-z_][a-z0-9_]*");
    }
    if (options.combat_policy_module.empty()) {
        options.combat_policy_module = options.profile + ".combat";
    }
    if (options.effects_policy_module.empty()) {
        options.effects_policy_module = options.profile + ".effects";
    }
    if (options.init_module.empty()) {
        options.init_module = options.profile + ".init";
    }
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

const std::string& Config::combat_policy_module() const noexcept
{
    return options_.combat_policy_module;
}

const std::string& Config::effects_policy_module() const noexcept
{
    return options_.effects_policy_module;
}

const std::string& Config::init_module() const noexcept
{
    return options_.init_module;
}

const std::string& Config::profile() const noexcept
{
    return options_.profile;
}

void Config::set_combat_policy_module(std::string module)
{
    options_.combat_policy_module = std::move(module);
}

void Config::set_init_module(std::string module)
{
    options_.init_module = std::move(module);
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
