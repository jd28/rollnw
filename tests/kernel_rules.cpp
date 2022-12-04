#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/util/game_install.hpp>
#include <nwn1/Profile.hpp>

#include <filesystem>

namespace nwk = nw::kernel;
namespace fs = std::filesystem;

TEST_CASE("rules system: class info", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    REQUIRE(nwk::rules().classes.get_is_class_skill(nwn1::class_type_barbarian, nwn1::skill_discipline));
    REQUIRE_FALSE(nwk::rules().classes.get_is_class_skill(nwn1::class_type_barbarian, nwn1::skill_tumble));
    REQUIRE_FALSE(nwk::rules().classes.get_is_class_skill(nw::Class::invalid(), nwn1::skill_craft_trap));
    REQUIRE_FALSE(nwk::rules().classes.get_is_class_skill(nwn1::class_type_barbarian, nw::Skill::invalid()));

    nw::Saves s;
    s = nwk::rules().classes.get_class_save_bonus(nwn1::class_type_rogue, 16);
    REQUIRE(s.fort == 5);
    REQUIRE(s.reflex == 10);
    REQUIRE(s.will == 5);

    s = nwk::rules().classes.get_class_save_bonus(nwn1::class_type_rogue, 90);
    REQUIRE(s.fort == 0);
    REQUIRE(s.reflex == 0);
    REQUIRE(s.will == 0);

    s = nwk::rules().classes.get_class_save_bonus(nw::Class::invalid(), 10);
    REQUIRE(s.fort == 0);
    REQUIRE(s.reflex == 0);
    REQUIRE(s.will == 0);

    REQUIRE(nwk::rules().classes.get_stat_gain(
                nwn1::class_type_dragon_disciple,
                nwn1::ability_strength, 10)
        == 8);

    REQUIRE(nwk::rules().classes.get_stat_gain(
                nwn1::class_type_dragon_disciple,
                nwn1::ability_constitution, 20)
        == 2);

    auto req = nwk::rules().classes.get_requirement(nwn1::class_type_dwarven_defender);
    REQUIRE(req);
    REQUIRE(req->main.size() == 4);
    nw::kernel::unload_module();
}

TEST_CASE("rules: master feats", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);

    int result = nw::kernel::sum_master_feats<int>(
        obj, nwn1::base_item_scimitar,
        nwn1::mfeat_weapon_focus, nwn1::mfeat_weapon_focus_epic);

    REQUIRE(result == 3);

    int result2 = nw::kernel::sum_master_feats<int>(
        obj, nwn1::base_item_gloves,
        nwn1::mfeat_weapon_focus, nwn1::mfeat_weapon_focus_epic);

    REQUIRE(result2 == 0);

    nw::kernel::unload_module();
}
