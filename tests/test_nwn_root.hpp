#pragma once

#include <nw/kernel/Kernel.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/log.hpp>
#include <nw/smalls/runtime.hpp>

#include <nowide/cstdlib.hpp>

#include <filesystem>
#include <optional>
#include <utility>

namespace nw::test {

// Test process configuration is genuinely singular: every test in the process
// must observe the same dedicated-server resource root.
inline std::optional<std::filesystem::path> dedicated_server_root()
{
    const auto* configured_root = nowide::getenv("NWN_ROOT");
    const std::filesystem::path install_root = configured_root && configured_root[0] != '\0'
        ? std::filesystem::path{configured_root}
        : std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "nwn";
    const auto base_key = install_root / "data/nwn_base.key";
    if (!std::filesystem::is_regular_file(base_key)) {
        LOG_F(ERROR, "Test NWN dedicated-server data is missing: {}", base_key.string());
        return std::nullopt;
    }

    return install_root;
}

// English dialog.tlk from the dedicated-server data. Tests must not carry a
// copy of it: the dedicated zip ensure_nwn.sh fetches already provides one, and
// a 13 MB string table per language in the repo is game content we do not ship.
inline std::optional<std::filesystem::path> dedicated_dialog_tlk()
{
    auto install_root = dedicated_server_root();
    if (!install_root) { return std::nullopt; }
    auto tlk = *install_root / "lang/en/data/dialog.tlk";
    if (!std::filesystem::is_regular_file(tlk)) {
        LOG_F(ERROR, "Test NWN dedicated-server dialog.tlk is missing: {}", tlk.string());
        return std::nullopt;
    }
    return tlk;
}

// The dedicated-server data carries every rules resource with real bytes --
// 2das, scripts, blueprints, walkmeshes, tilesets, palettes -- but **no visual
// payloads**. Models, textures and PLT layers are present in its KEY index and
// are zero bytes in the BIFs (mdl 32,787 / tga 28,196 / dds 6,737 / plt 1,565,
// all empty). Anything needing those wants a full consumer install (GOG, Steam,
// Beamdog client) and must skip without one.
//
// This probes by payload, not by file existence or index presence, because the
// server install answers "yes" to both for resources it does not actually ship.
inline bool has_full_game_assets()
{
    if (!dedicated_server_root()) { return false; }
    // Core placeable model, present in every real install.
    const auto model = nw::kernel::resman().demand(
        nw::Resource{nw::Resref{"plc_a01"}, nw::ResourceType::mdl});
    return model.bytes.size() > 0;
}

inline bool configure_dedicated_server(std::filesystem::path user_root)
{
    auto install_root = dedicated_server_root();
    if (!install_root) { return false; }

    nw::kernel::config().set_paths(*install_root, std::move(user_root));
    return true;
}

inline void register_source_nwn1_packages()
{
    const auto scripts = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR}
        / "lib/nw/smalls/scripts";
    nw::kernel::services().create();
    nw::kernel::runtime().add_module_path(scripts / "core");
    nw::kernel::runtime().add_module_path(scripts / "nwn1");
}

} // namespace nw::test
