#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Spell.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/items.hpp>
#include <nwn1/Profile.hpp>

#include <nw/util/game_install.hpp>

namespace nwk = nw::kernel;

TEST_CASE("rules manager", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    //     auto* abs = nwk::world().get<nw::AbilityArray>();
    //     REQUIRE(abs);
    //     REQUIRE(abs->entries.size() == 6);

    //     auto* ba = nwk::world().get<nw::BaseItemArray>();
    //     REQUIRE(ba);
    //     REQUIRE(ba->entries.size() > 0);

    //     auto* cl = nwk::world().get<nw::ClassArray>();
    //     REQUIRE(cl);
    //     REQUIRE(cl->entries.size() > 0);

    //     auto* ft = nwk::world().get<nw::FeatArray>();
    //     REQUIRE(ft);
    //     REQUIRE(ft->entries.size() > 0);

    //     auto* ra = nwk::world().get<nw::RaceArray>();
    //     REQUIRE(ra);
    //     REQUIRE(ra->entries.size() > 0);

    //     auto* sk = nwk::world().get<nw::SkillArray>();
    //     REQUIRE(sk);
    //     REQUIRE(sk->entries.size() > 0);

    //     auto* sp = nwk::world().get<nw::SpellArray>();
    //     REQUIRE(sp);
    //     REQUIRE(sp->entries.size() > 0);

    //     auto& skill = sk->entries[0];
    //     REQUIRE(skill.valid());
    //     REQUIRE(skill.name == 269);
    //     REQUIRE(skill.constant == "SKILL_ANIMAL_EMPATHY");

    REQUIRE(nwk::services().get_mut<nwk::TwoDACache>()->cache("placeables"sv));
    REQUIRE(nwk::services().get_mut<nwk::TwoDACache>()->get("placeables"sv)->rows() > 0);

    nw::kernel::unload_module();
}

TEST_CASE("rules system item property cost/param tables", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    REQUIRE(nwk::rules().ip_cost_table(4));
    REQUIRE(nwk::rules().ip_param_table(3));
    REQUIRE(nwk::rules().ip_definition(nwn1::ip_ability_bonus)->name == 649);

    nw::kernel::unload_module();
}
