#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/rules/system.hpp>

TEST_CASE("selectors", "[rules]")
{
    auto mod = nw::kernel::load_module(new nwn1::Profile, "test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    auto sel1 = nw::select::ability(nwn1::ability_strength);
    // REQUIRE(sel1.subtype == "ABILITY_STRENGTH");
    REQUIRE(sel1.select(c).as<int32_t>() == 40);

    auto sel2 = nw::select::ability(nwn1::ability_constitution);
    REQUIRE(sel2.select(c).as<int32_t>() == 16);

    auto sel3 = nw::select::skill(nwn1::skill_tumble);
    REQUIRE(sel3.select(c).as<int32_t>() == 0);

    auto sel4 = nw::select::skill(nwn1::skill_discipline);
    REQUIRE(sel4.type == nw::SelectorType::skill);
    REQUIRE(sel4.subtype);
    REQUIRE(sel4.subtype == 3);
    REQUIRE(sel4.select(c).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: level", "[rules]")
{
    auto mod = nw::kernel::load_module(new nwn1::Profile, "test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    auto sel1 = nw::select::level();
    REQUIRE(sel1.select(c).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: feat", "[rules]")
{
    auto mod = nw::kernel::load_module(new nwn1::Profile, "test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);
    auto* cr = nw::kernel::world().get<nw::ConstantRegistry>();

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};
    const auto feat_improved_critical_creature = cr->get("FEAT_IMPROVED_CRITICAL_CREATURE");

    auto sel1 = nw::select::feat(feat_improved_critical_creature);
    REQUIRE(sel1.select(c).as<int32_t>());

    nw::kernel::unload_module();
}
