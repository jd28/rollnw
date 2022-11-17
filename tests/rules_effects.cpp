#include "nw/rules/attributes.hpp"
#include "nwn1/constants.hpp"
#include "nwn1/effects.hpp"
#include <catch2/catch_test_macros.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/Archives.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("effects", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::Effect eff;
    eff.set_string(12, "my string");
    REQUIRE(eff.get_string(12) == "my string");
    REQUIRE(eff.get_int(3) == 0);

    nw::kernel::unload_module();
}

TEST_CASE("item properties", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ip = nwn1::itemprop_haste();
    auto str = nwn1::itemprop_to_string(ip);
    REQUIRE(str == "Haste");

    auto ip2 = nwn1::itemprop_ability_modifier(nwn1::ability_strength, 6);
    auto str2 = nwn1::itemprop_to_string(ip2);
    REQUIRE(str2 == "Enhancement Bonus: Strength +6");

    nw::kernel::unload_module();
}
