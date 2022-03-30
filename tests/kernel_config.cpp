#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/util/game_install.hpp>

TEST_CASE("config manager", "[kernel]")
{
    nw::InstallInfo info{
        "test_data/",
        "test_data/",
        nw::InstallVersion::vEE};
    nw::kernel::config().load(info);

    REQUIRE(nw::kernel::config().nwn_ini().valid());
    REQUIRE(!!nw::kernel::config().nwn_ini().get<std::string>("Alias/TEMP"));
    REQUIRE(*nw::kernel::config().nwn_ini().get<std::string>("alias/temp") == "/Users/xxx/Documents/Neverwinter Nights/temp");

    REQUIRE_FALSE(nw::kernel::config().alias_path(nw::PathAlias::development).empty());
    REQUIRE(nw::kernel::config().resolve_alias("HAK:test.hak")
        == nw::kernel::config().alias_path(nw::PathAlias::hak) / "test.hak");

    if (info.version == nw::InstallVersion::vEE) {
        REQUIRE_FALSE(nw::kernel::config().settings_tml().empty());
        REQUIRE(*nw::kernel::config().settings_tml()["game"]["gore"].as<int64_t>() == 1);
    } else {
        REQUIRE(nw::kernel::config().settings_tml().empty());
    }
}
