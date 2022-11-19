#include <catch2/catch_all.hpp>

#include <nw/components/Creature.hpp>
#include <nw/components/Player.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/combat.hpp>
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

TEST_CASE("player: level history", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto pl = nwk::objects().load_player("CDKEY", "testsorcpc1");
    REQUIRE(pl);
    REQUIRE(pl->history.entries.size() == 15);
    REQUIRE(pl->history.entries[0].known_spells.size() == 6);

    nwk::unload_module();
}

TEST_CASE("player: to gffjson", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ba = nwk::resman().demand_server_vault("CDKEY", "testwizardpc1");
    nw::Gff g{std::move(ba)};
    REQUIRE(g.valid());

    auto j = nw::gff_to_gffjson(g);
    std::ofstream out{"tmp/testwizardpc1.bic.gffjson"};
    out << std::setw(4) << j;

    nwk::unload_module();
}
