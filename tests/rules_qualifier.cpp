#include <catch2/catch_all.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/rules/system.hpp>
#include <nwn1/Profile.hpp>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("qualifier", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto qual1 = nwn1::qual::ability(nwn1::ability_strength, 0, 20); // less than 20 str.
    REQUIRE_FALSE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::ability(nwn1::ability_constitution, 15, 20); // between 15 and 20
    REQUIRE(nwn1::match(qual2, ent));

    auto qual3 = nwn1::qual::skill(nwn1::skill_discipline, 35); // at least 35
    REQUIRE(nwn1::match(qual3, ent));

    nwk::unload_module();
}

TEST_CASE("qualifier: race", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto qual1 = nwn1::qual::race(nwn1::racial_type_human);
    REQUIRE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::race(nwn1::racial_type_ooze);
    REQUIRE_FALSE(nwn1::match(qual2, ent));

    nwk::unload_module();
}

TEST_CASE("qualifier: level", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto qual1 = nwn1::qual::level(0, 1);
    REQUIRE_FALSE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::level(1);
    REQUIRE(nwn1::match(qual2, ent));

    nwk::unload_module();
}

TEST_CASE("qualifier: alignment", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto ent2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(ent2);

    auto qual1 = nwn1::qual::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::good);

    REQUIRE(nwn1::match(qual1, ent));
    REQUIRE_FALSE(nwn1::match(qual1, ent2));

    auto qual2 = nwn1::qual::alignment(nw::AlignmentAxis::both,
        nw::AlignmentFlags::neutral);

    REQUIRE_FALSE(nwn1::match(qual2, ent));
    REQUIRE(nwn1::match(qual2, ent2));

    auto qual3 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful);

    REQUIRE_FALSE(nwn1::match(qual3, ent));
    REQUIRE_FALSE(nwn1::match(qual3, ent2));

    auto qual4 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::chaotic);

    REQUIRE_FALSE(nwn1::match(qual4, ent));
    REQUIRE_FALSE(nwn1::match(qual4, ent2));

    auto qual5 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::lawful | nw::AlignmentFlags::neutral);

    REQUIRE(nwn1::match(qual5, ent));
    REQUIRE(nwn1::match(qual5, ent2));

    auto qual6 = nwn1::qual::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::evil);

    REQUIRE_FALSE(nwn1::match(qual6, ent));
    REQUIRE_FALSE(nwn1::match(qual6, ent2));

    auto qual7 = nwn1::qual::alignment(nw::AlignmentAxis::good_evil,
        nw::AlignmentFlags::neutral | nw::AlignmentFlags::good);

    REQUIRE(nwn1::match(qual7, ent));
    REQUIRE(nwn1::match(qual7, ent2));

    auto qual8 = nwn1::qual::alignment(nw::AlignmentAxis::law_chaos,
        nw::AlignmentFlags::neutral);

    REQUIRE(nwn1::match(qual8, ent));
    REQUIRE(nwn1::match(qual8, ent2));

    nwk::unload_module();
}

TEST_CASE("qualifier: class_level", "[rules]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    auto qual1 = nwn1::qual::class_level(nwn1::class_type_fighter, 30, 40);
    REQUIRE_FALSE(nwn1::match(qual1, ent));

    auto qual2 = nwn1::qual::class_level(nwn1::class_type_fighter, 10);
    REQUIRE(nwn1::match(qual2, ent));

    auto qual3 = nwn1::qual::class_level(nwn1::class_type_fighter, 1, 1);
    REQUIRE_FALSE(nwn1::match(qual3, ent));

    auto qual4 = nwn1::qual::class_level(nwn1::class_type_fighter, 4, 5);
    REQUIRE_FALSE(nwn1::match(qual4, ent));

    nwk::unload_module();
}
