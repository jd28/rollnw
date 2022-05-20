#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/rules/system.hpp>

namespace fs = std::filesystem;

TEST_CASE("qualifier", "[rules]")
{
    auto mod = nw::kernel::load_module(new nwn1::Profile, "test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    REQUIRE_FALSE(nwn1::ability_strength.empty());
    REQUIRE_FALSE(nwn1::ability_constitution.empty());
    REQUIRE_FALSE(nwn1::skill_tumble.empty());
    REQUIRE_FALSE(nwn1::skill_discipline.empty());

    auto qual1 = nw::qualifier::ability(nwn1::ability_strength, 0, 20); // less than 20 str.
    REQUIRE_FALSE(qual1.match(ent));

    auto qual2 = nw::qualifier::ability(nwn1::ability_constitution, 15, 20); // between 15 and 20
    REQUIRE(qual2.match(ent));

    auto qual3 = nw::qualifier::skill(nwn1::skill_discipline, 35); // at least 35
    REQUIRE(qual3.match(ent));

    nw::kernel::unload_module();
}

TEST_CASE("qualifier: race", "[rules]")
{
    auto mod = nw::kernel::load_module(new nwn1::Profile, "test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto qual1 = nw::qualifier::race(nwn1::racial_type_human);
    REQUIRE(qual1.match(ent));

    auto qual2 = nw::qualifier::race(nwn1::racial_type_ooze);
    REQUIRE_FALSE(qual2.match(ent));

    nw::kernel::unload_module();
}

TEST_CASE("qualifier: level", "[rules]")
{
    auto mod = nw::kernel::load_module(new nwn1::Profile, "test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto qual1 = nw::qualifier::level(0, 1);
    REQUIRE_FALSE(qual1.match(ent));

    auto qual2 = nw::qualifier::level(1);
    REQUIRE(qual2.match(ent));

    nw::kernel::unload_module();
}
