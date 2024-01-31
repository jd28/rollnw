#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/util/game_install.hpp>

TEST(Config, Basic)
{
    auto& config = nw::kernel::config();
    EXPECT_TRUE(config.nwn_ini().valid());

    if (config.version() == nw::GameVersion::vEE) {
        EXPECT_FALSE(config.settings_tml().empty());
        EXPECT_EQ(*config.settings_tml()["game"]["gore"].as<int64_t>(), 1);
    } else {
        EXPECT_TRUE(config.settings_tml().empty());
    }
}
