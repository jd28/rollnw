#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/util/game_install.hpp>

TEST_CASE("config manager", "[kernel]")
{
    auto& config = nw::kernel::config();
    REQUIRE(config.nwn_ini().valid());
    REQUIRE(!!config.nwn_ini().get<std::string>("Alias/TEMP"));
    // REQUIRE(*config.nwn_ini().get<std::string>("alias/temp") == "/Users/xxx/Documents/Neverwinter Nights/temp");

    REQUIRE_FALSE(config.alias_path(nw::PathAlias::development).empty());
    REQUIRE(config.resolve_alias("HAK:test.hak")
        == config.alias_path(nw::PathAlias::hak) / "test.hak");

    if (config.options().version == nw::GameVersion::vEE) {
        REQUIRE_FALSE(config.settings_tml().empty());
        REQUIRE(*config.settings_tml()["game"]["gore"].as<int64_t>() == 1);
    } else {
        REQUIRE(config.settings_tml().empty());
    }
}
