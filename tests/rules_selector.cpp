#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/components/IndexRegistry.hpp>
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

    auto sel1 = nwn1::sel::ability(nwn1::ability_strength);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    auto sel2 = nwn1::sel::ability(nwn1::ability_constitution);
    REQUIRE(nwk::rules().select(sel2, ent).as<int32_t>() == 16);

    auto sel3 = nwn1::sel::skill(nwn1::skill_tumble);
    REQUIRE(nwk::rules().select(sel3, ent).as<int32_t>() == 0);

    auto sel4 = nwn1::sel::skill(nwn1::skill_discipline);
    REQUIRE(sel4.type == nw::SelectorType::skill);
    REQUIRE(sel4.subtype.is<nw::Index>());
    REQUIRE(sel4.subtype.as<nw::Index>() == 3u);
    REQUIRE(nwk::rules().select(sel4, ent).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: level", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto sel1 = nwn1::sel::level();
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: feat", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);
    auto* cr = nw::kernel::world().get<nw::IndexRegistry>();

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    const auto feat_improved_critical_creature = cr->get("FEAT_IMPROVED_CRITICAL_CREATURE");

    auto sel1 = nwn1::sel::feat(feat_improved_critical_creature);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>());

    nw::kernel::unload_module();
}

TEST_CASE("selector: class_level", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);
    auto* cr = nw::kernel::world().get<nw::IndexRegistry>();

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    const auto class_type_fighter = cr->get("CLASS_TYPE_FIGHTER");
    REQUIRE_FALSE(class_type_fighter.empty());

    auto sel1 = nwn1::sel::class_level(class_type_fighter);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 10);

    nw::kernel::unload_module();
}

TEST_CASE("selector: alignment", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto sel1 = nwn1::sel::alignment(nw::AlignmentAxis::law_chaos);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 50);

    auto sel2 = nwn1::sel::alignment(nw::AlignmentAxis::good_evil);
    REQUIRE(nwk::rules().select(sel2, ent).as<int32_t>() == 100);

    nw::kernel::unload_module();
}
