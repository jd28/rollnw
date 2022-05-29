#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/rules/system.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("qualifier", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
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

    nwk::unload_module();
}

TEST_CASE("qualifier: race", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto qual1 = nw::qualifier::race(nwn1::racial_type_human);
    REQUIRE(qual1.match(ent));

    auto qual2 = nw::qualifier::race(nwn1::racial_type_ooze);
    REQUIRE_FALSE(qual2.match(ent));

    nwk::unload_module();
}

TEST_CASE("qualifier: level", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto qual1 = nw::qualifier::level(0, 1);
    REQUIRE_FALSE(qual1.match(ent));

    auto qual2 = nw::qualifier::level(1);
    REQUIRE(qual2.match(ent));

    nwk::unload_module();
}

TEST_CASE("qualifier: alignment", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto ent2 = nwk::objects().load(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(ent2.is_alive());

    auto qual1 = nw::qualifier::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::good);

    REQUIRE(qual1.match(ent));
    REQUIRE_FALSE(qual1.match(ent2));

    auto qual2 = nw::qualifier::alignment(nw::AlignmentAxis::both,
        nw::AlignmentFlags::neutral);

    REQUIRE_FALSE(qual2.match(ent));
    REQUIRE(qual2.match(ent2));

    auto qual3 = nw::qualifier::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful);

    REQUIRE_FALSE(qual3.match(ent));
    REQUIRE_FALSE(qual3.match(ent2));

    auto qual4 = nw::qualifier::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::chaotic);

    REQUIRE_FALSE(qual4.match(ent));
    REQUIRE_FALSE(qual4.match(ent2));

    auto qual5 = nw::qualifier::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful | nw::AlignmentFlags::neutral);

    REQUIRE(qual5.match(ent));
    REQUIRE(qual5.match(ent2));

    auto qual6 = nw::qualifier::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::evil);

    REQUIRE_FALSE(qual6.match(ent));
    REQUIRE_FALSE(qual6.match(ent2));

    auto qual7 = nw::qualifier::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::neutral | nw::AlignmentFlags::good);

    REQUIRE(qual7.match(ent));
    REQUIRE(qual7.match(ent2));

    auto qual8 = nw::qualifier::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::neutral);

    REQUIRE(qual8.match(ent));
    REQUIRE(qual8.match(ent2));

    nwk::unload_module();
}
