#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/rules/Ability.hpp>
#include <nw/rules/BaseItem.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/rules/Race.hpp>
#include <nw/rules/Skill.hpp>
#include <nw/rules/Spell.hpp>
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
