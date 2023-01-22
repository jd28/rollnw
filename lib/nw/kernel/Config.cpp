#include "Config.hpp"

#include "../log.hpp"
#include "../util/platform.hpp"
#include "../util/string.hpp"

namespace fs = std::filesystem;

namespace nw::kernel {

fs::path Config::alias_path(PathAlias alias)
{
    std::optional<std::string> str;
    switch (alias) {
    default:
        return {};
    case PathAlias::ambient:
        str = nwn_ini_.get<std::string>("alias/ambient");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::cache:
        str = nwn_ini_.get<std::string>("alias/cache");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::currentgame:
        str = nwn_ini_.get<std::string>("alias/currentgame");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::database:
        str = nwn_ini_.get<std::string>("alias/database");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::development:
        str = nwn_ini_.get<std::string>("alias/development");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::dmvault:
        str = nwn_ini_.get<std::string>("alias/dmvault");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::hak:
        str = nwn_ini_.get<std::string>("alias/hak");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::hd0:
        str = nwn_ini_.get<std::string>("alias/hd0");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::localvault:
        str = nwn_ini_.get<std::string>("alias/localvault");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::logs:
        str = nwn_ini_.get<std::string>("alias/logs");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::modelcompiler:
        str = nwn_ini_.get<std::string>("alias/modelcompiler");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::modules:
        str = nwn_ini_.get<std::string>("alias/modules");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::movies:
        str = nwn_ini_.get<std::string>("alias/movies");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::music:
        str = nwn_ini_.get<std::string>("alias/music");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::nwsync:
        str = nwn_ini_.get<std::string>("alias/nwsync");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::oldservervault:
        str = nwn_ini_.get<std::string>("alias/oldservervault");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::override:
        str = nwn_ini_.get<std::string>("alias/override");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::patch:
        str = nwn_ini_.get<std::string>("alias/patch");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::portraits:
        str = nwn_ini_.get<std::string>("alias/portraits");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::saves:
        str = nwn_ini_.get<std::string>("alias/saves");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::screenshots:
        str = nwn_ini_.get<std::string>("alias/screenshots");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::servervault:
        str = nwn_ini_.get<std::string>("alias/servervault");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::temp:
        str = nwn_ini_.get<std::string>("alias/temp");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::tempclient:
        str = nwn_ini_.get<std::string>("alias/tempclient");
        return str ? fs::path(*str) : fs::path{};
    case PathAlias::tlk:
        str = nwn_ini_.get<std::string>("alias/tlk");
        return str ? fs::path(*str) : fs::path{};
    }
}

void Config::initialize(ConfigOptions options)
{
    options_ = std::move(options);
    LOG_F(INFO, "kernel: initializing config system");

    if (options_.version == GameVersion::invalid) {
        LOG_F(ERROR, "Failed to find valid NWN(:EE) install.");
        return;
    }

    if (options_.version == GameVersion::vEE) {
        auto path = options_.user / "nwn.ini";
        if (fs::exists(path)) { nwn_ini_ = Ini{path}; }

        path = options_.user / "nwnplayer.ini";
        if (fs::exists(path)) { nwnplayer_ini_ = Ini{path}; }

        path = options_.user / "userpatch.ini";
        if (fs::exists(path)) { userpatch_ini_ = Ini{path}; }

        path = options_.user / "settings.tml";
        if (fs::exists(path)) {
            settings_tml_ = toml::parse_file(path_to_string(path));
        }
    } else if (options_.version == GameVersion::v1_69) {
        auto path = options_.user / "nwn.ini";
        if (fs::exists(path)) { nwn_ini_ = Ini{path}; }

        path = options_.user / "nwnplayer.ini";
        if (fs::exists(path)) { nwnplayer_ini_ = Ini{path}; }

        path = options_.user / "nwnpatch.ini";
        if (fs::exists(path)) { userpatch_ini_ = Ini{path}; }
    }
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

fs::path Config::resolve_alias(std::string_view alias_path) const
{
    std::optional<std::string> str;

    if (string::startswith(alias_path, "AMBIENT:")) {
        str = nwn_ini_.get<std::string>("alias/ambient");
        return str ? fs::path(*str) / alias_path.substr(8) : fs::path{};
    } else if (string::startswith(alias_path, "CACHE:")) {
        str = nwn_ini_.get<std::string>("alias/cache");
        return str ? fs::path(*str) / alias_path.substr(6) : fs::path{};
    } else if (string::startswith(alias_path, "CURRENTGAME:")) {
        str = nwn_ini_.get<std::string>("alias/currentgame");
        return str ? fs::path(*str) / alias_path.substr(11) : fs::path{};
    } else if (string::startswith(alias_path, "DATABASE:")) {
        str = nwn_ini_.get<std::string>("alias/database");
        return str ? fs::path(*str) / alias_path.substr(9) : fs::path{};
    } else if (string::startswith(alias_path, "DEVELOPMENT:")) {
        str = nwn_ini_.get<std::string>("alias/development");
        return str ? fs::path(*str) / alias_path.substr(11) : fs::path{};
    } else if (string::startswith(alias_path, "DMVAULT:")) {
        str = nwn_ini_.get<std::string>("alias/dmvault");
        return str ? fs::path(*str) / alias_path.substr(8) : fs::path{};
    } else if (string::startswith(alias_path, "HAK:")) {
        str = nwn_ini_.get<std::string>("alias/hak");
        return str ? fs::path(*str) / alias_path.substr(4) : fs::path{};
    } else if (string::startswith(alias_path, "HD0:")) {
        str = nwn_ini_.get<std::string>("alias/hd0");
        return str ? fs::path(*str) / alias_path.substr(4) : fs::path{};
    } else if (string::startswith(alias_path, "LOCALVAULT:")) {
        str = nwn_ini_.get<std::string>("alias/localvault");
        return str ? fs::path(*str) / alias_path.substr(11) : fs::path{};
    } else if (string::startswith(alias_path, "LOGS:")) {
        str = nwn_ini_.get<std::string>("alias/logs");
        return str ? fs::path(*str) / alias_path.substr(5) : fs::path{};
    } else if (string::startswith(alias_path, "MODELCOMPILER:")) {
        str = nwn_ini_.get<std::string>("alias/modelcompiler");
        return str ? fs::path(*str) / alias_path.substr(13) : fs::path{};
    } else if (string::startswith(alias_path, "MODULES:")) {
        str = nwn_ini_.get<std::string>("alias/modules");
        return str ? fs::path(*str) / alias_path.substr(8) : fs::path{};
    } else if (string::startswith(alias_path, "MOVIES:")) {
        str = nwn_ini_.get<std::string>("alias/movies");
        return str ? fs::path(*str) / alias_path.substr(7) : fs::path{};
    } else if (string::startswith(alias_path, "MUSIC:")) {
        str = nwn_ini_.get<std::string>("alias/music");
        return str ? fs::path(*str) / alias_path.substr(6) : fs::path{};
    } else if (string::startswith(alias_path, "NWSYNC:")) {
        str = nwn_ini_.get<std::string>("alias/nwsync");
        return str ? fs::path(*str) / alias_path.substr(7) : fs::path{};
    } else if (string::startswith(alias_path, "OLDSERVERVAULT:")) {
        str = nwn_ini_.get<std::string>("alias/oldservervault");
        return str ? fs::path(*str) / alias_path.substr() : fs::path{};
    } else if (string::startswith(alias_path, "OVERRIDE:")) {
        str = nwn_ini_.get<std::string>("alias/override");
        return str ? fs::path(*str) / alias_path.substr(9) : fs::path{};
    } else if (string::startswith(alias_path, "PATCH:")) {
        str = nwn_ini_.get<std::string>("alias/patch");
        return str ? fs::path(*str) / alias_path.substr(6) : fs::path{};
    } else if (string::startswith(alias_path, "PORTRAITS:")) {
        str = nwn_ini_.get<std::string>("alias/portraits");
        return str ? fs::path(*str) / alias_path.substr(10) : fs::path{};
    } else if (string::startswith(alias_path, "SAVES:")) {
        str = nwn_ini_.get<std::string>("alias/saves");
        return str ? fs::path(*str) / alias_path.substr(6) : fs::path{};
    } else if (string::startswith(alias_path, "SCREENSHOTS:")) {
        str = nwn_ini_.get<std::string>("alias/screenshots");
        return str ? fs::path(*str) / alias_path.substr(12) : fs::path{};
    } else if (string::startswith(alias_path, "SERVERVAULT:")) {
        str = nwn_ini_.get<std::string>("alias/servervault");
        return str ? fs::path(*str) / alias_path.substr(12) : fs::path{};
    } else if (string::startswith(alias_path, "TEMP:")) {
        str = nwn_ini_.get<std::string>("alias/temp");
        return str ? fs::path(*str) / alias_path.substr(5) : fs::path{};
    } else if (string::startswith(alias_path, "TEMPCLIENT:")) {
        str = nwn_ini_.get<std::string>("alias/tempclient");
        return str ? fs::path(*str) / alias_path.substr(11) : fs::path{};
    } else if (string::startswith(alias_path, "TLK:")) {
        str = nwn_ini_.get<std::string>("alias/tlk");
        return str ? fs::path(*str) / alias_path.substr(4) : fs::path{};
    }

    return {};
}

const toml::table& Config::settings_tml() const noexcept
{
    return settings_tml_;
}

const Ini& Config::userpatch_ini() const noexcept
{
    return userpatch_ini_;
}

} // namespace nw::kernel
