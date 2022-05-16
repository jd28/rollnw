#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/profiles/nwn1/RuleProfile.hpp>
#include <nw/rules/Ability.hpp>
#include <nw/rules/BaseItem.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/rules/Race.hpp>
#include <nw/rules/Skill.hpp>
#include <nw/rules/Spell.hpp>

#include <nw/util/game_install.hpp>

TEST_CASE("rules manager", "[kernel]")
{
    auto mod = nw::kernel::load_module(new nwn1::Profile, "test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto* abs = nw::kernel::world().get<nw::AbilityArray>();
    REQUIRE(abs);
    REQUIRE(abs->abiliites.size() == 6);

    auto* ba = nw::kernel::world().get<nw::BaseItemArray>();
    REQUIRE(ba);
    REQUIRE(ba->baseitems.size() > 0);

    auto* cl = nw::kernel::world().get<nw::ClassArray>();
    REQUIRE(cl);
    REQUIRE(cl->classes.size() > 0);

    auto* ft = nw::kernel::world().get<nw::FeatArray>();
    REQUIRE(ft);
    REQUIRE(ft->feats.size() > 0);

    auto* ra = nw::kernel::world().get<nw::RaceArray>();
    REQUIRE(ra);
    REQUIRE(ra->races.size() > 0);

    auto* sk = nw::kernel::world().get<nw::SkillArray>();
    REQUIRE(sk);
    REQUIRE(sk->skills.size() > 0);

    auto* sp = nw::kernel::world().get<nw::SpellArray>();
    REQUIRE(sp);
    REQUIRE(sp->spells.size() > 0);

    auto& skill = sk->skills[0];
    REQUIRE(skill);
    REQUIRE(skill.name == 269);
    REQUIRE(skill.constant.name == "SKILL_ANIMAL_EMPATHY");

    auto place2da = nw::Resource{"placeables"sv, nw::ResourceType::twoda};
    REQUIRE(nw::kernel::rules().cache_2da(place2da));
    REQUIRE(nw::kernel::rules().get_cached_2da(place2da).rows() > 0);

    nw::kernel::unload_module();
}
