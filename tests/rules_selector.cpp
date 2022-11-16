#include <catch2/catch_all.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/system.hpp>
#include <nwn1/Profile.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("selectors", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto sel1 = nwn1::sel::ability(nwn1::ability_strength);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    auto sel2 = nwn1::sel::ability(nwn1::ability_constitution);
    REQUIRE(nwk::rules().select(sel2, ent).as<int32_t>() == 16);

    auto sel3 = nwn1::sel::skill(nwn1::skill_tumble);
    REQUIRE(nwk::rules().select(sel3, ent).as<int32_t>() == 6);

    auto sel4 = nwn1::sel::skill(nwn1::skill_discipline);
    REQUIRE(sel4.type == nw::SelectorType::skill);
    REQUIRE(sel4.subtype.is<int32_t>());
    REQUIRE(sel4.subtype.as<int32_t>() == 3);
    REQUIRE(nwk::rules().select(sel4, ent).as<int32_t>() == 58);

    nw::kernel::unload_module();
}

TEST_CASE("selector: level", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto sel1 = nwn1::sel::level();
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 40);

    nw::kernel::unload_module();
}

TEST_CASE("selector: race", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto sel1 = nwn1::sel::race();
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == *nwn1::racial_type_human);

    nw::kernel::unload_module();
}

TEST_CASE("selector: feat", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto sel1 = nwn1::sel::feat(nwn1::feat_improved_critical_creature);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>());

    nw::kernel::unload_module();
}

TEST_CASE("selector: class_level", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto sel1 = nwn1::sel::class_level(nwn1::class_type_fighter);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 10);

    nw::kernel::unload_module();
}

TEST_CASE("selector: alignment", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto sel1 = nwn1::sel::alignment(nw::AlignmentAxis::law_chaos);
    REQUIRE(nwk::rules().select(sel1, ent).as<int32_t>() == 50);

    auto sel2 = nwn1::sel::alignment(nw::AlignmentAxis::good_evil);
    REQUIRE(nwk::rules().select(sel2, ent).as<int32_t>() == 100);

    nw::kernel::unload_module();
}
