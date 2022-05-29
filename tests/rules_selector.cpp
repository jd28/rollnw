#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/components/ConstantRegistry.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/rules/system.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("selectors", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto sel1 = nw::select::ability(nwn1::ability_strength);
    // REQUIRE(sel1.subtype == "ABILITY_STRENGTH");
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    auto sel2 = nw::select::ability(nwn1::ability_constitution);
    REQUIRE(nwk::rules().select(sel2, ent).as<int32_t>() == 16);

    auto sel3 = nw::select::skill(nwn1::skill_tumble);
    REQUIRE(nwk::rules().select(sel3, ent).as<int32_t>() == 0);

    auto sel4 = nw::select::skill(nwn1::skill_discipline);
    REQUIRE(sel4.type == nw::SelectorType::skill);
    REQUIRE(sel4.subtype);
    REQUIRE(sel4.subtype == 3);
    REQUIRE(nwk::rules().select(sel4, ent).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: level", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto sel1 = nw::select::level();
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: feat", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);
    auto* cr = nw::kernel::world().get<nw::ConstantRegistry>();

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    const auto feat_improved_critical_creature = cr->get("FEAT_IMPROVED_CRITICAL_CREATURE");

    auto sel1 = nw::select::feat(feat_improved_critical_creature);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>());

    nw::kernel::unload_module();
}

TEST_CASE("selector: alignment", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto sel1 = nw::select::alignment(nw::AlignmentAxis::law_chaos);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 50);

    auto sel2 = nw::select::alignment(nw::AlignmentAxis::good_evil);
    REQUIRE(nwk::rules().select(sel2, ent).as<int32_t>() == 100);

    nw::kernel::unload_module();
}
