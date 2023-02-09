#include <gtest/gtest.h>

#include <nw/functions.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/Archives.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/constants.hpp>
#include <nwn1/effects.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Rules, Effects)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    nw::Effect eff;
    eff.set_string(12, "my string");
    EXPECT_EQ(eff.get_string(12), "my string");
    EXPECT_EQ(eff.get_int(3), 0);

    nw::kernel::unload_module();
}

TEST(Rules, ItemProperties)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ip = nwn1::itemprop_haste();
    auto str = nw::itemprop_to_string(ip);
    EXPECT_EQ(str, "Haste");

    auto ip2 = nwn1::itemprop_ability_modifier(nwn1::ability_strength, 6);
    auto str2 = nw::itemprop_to_string(ip2);
    EXPECT_EQ(str2, "Enhancement Bonus: Strength +6");

    nw::kernel::unload_module();
}
