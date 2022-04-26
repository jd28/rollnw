#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/system.hpp>

TEST_CASE("selectors", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    const auto ability_strength = nw::kernel::rules().get_constant("ABILITY_STRENGTH");
    const auto ability_con = nw::kernel::rules().get_constant("ABILITY_CONSTITUTION");
    const auto skill_tumble = nw::kernel::rules().get_constant("SKILL_TUMBLE");
    const auto skill_disc = nw::kernel::rules().get_constant("SKILL_DISCIPLINE");

    auto sel1 = nw::select::ability(ability_strength);
    REQUIRE(sel1.subtype.name.view() == "ABILITY_STRENGTH");
    REQUIRE(*sel1.select(c) == 40);

    auto sel2 = nw::select::ability(ability_con);
    REQUIRE(*sel2.select(c) == 16);

    auto sel3 = nw::select::skill(skill_tumble);
    REQUIRE(*sel3.select(c) == 0);

    auto sel4 = nw::select::skill(skill_disc);
    REQUIRE(sel4.type == nw::SelectorType::skill);
    REQUIRE(sel4.subtype);
    REQUIRE(sel4.subtype.name.view() == "SKILL_DISCIPLINE");
    REQUIRE(sel4.subtype.as_index() == 3);
    REQUIRE(*sel4.select(c) == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: level", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::GffInputArchive g{"test_data/user/development/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    auto sel1 = nw::select::level();
    REQUIRE(*sel1.select(c) == 40);

    nw::kernel::unload_module();
}
