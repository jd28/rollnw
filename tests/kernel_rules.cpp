#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/util/game_install.hpp>
#include <nwn1/Profile.hpp>

#include <filesystem>

namespace nwk = nw::kernel;
namespace fs = std::filesystem;

TEST(KernelRules, Classes)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    EXPECT_TRUE(nwk::rules().classes.get_is_class_skill(nwn1::class_type_barbarian, nwn1::skill_discipline));
    EXPECT_FALSE(nwk::rules().classes.get_is_class_skill(nwn1::class_type_barbarian, nwn1::skill_tumble));
    EXPECT_FALSE(nwk::rules().classes.get_is_class_skill(nw::Class::invalid(), nwn1::skill_craft_trap));
    EXPECT_FALSE(nwk::rules().classes.get_is_class_skill(nwn1::class_type_barbarian, nw::Skill::invalid()));

    nw::Saves s;
    s = nwk::rules().classes.get_class_save_bonus(nwn1::class_type_rogue, 16);
    EXPECT_EQ(s.fort, 5);
    EXPECT_EQ(s.reflex, 10);
    EXPECT_EQ(s.will, 5);

    s = nwk::rules().classes.get_class_save_bonus(nwn1::class_type_rogue, 90);
    EXPECT_EQ(s.fort, 0);
    EXPECT_EQ(s.reflex, 0);
    EXPECT_EQ(s.will, 0);

    s = nwk::rules().classes.get_class_save_bonus(nw::Class::invalid(), 10);
    EXPECT_EQ(s.fort, 0);
    EXPECT_EQ(s.reflex, 0);
    EXPECT_EQ(s.will, 0);

    EXPECT_EQ(nwk::rules().classes.get_stat_gain(
                  nwn1::class_type_dragon_disciple,
                  nwn1::ability_strength, 10),
        8);

    EXPECT_EQ(nwk::rules().classes.get_stat_gain(
                  nwn1::class_type_dragon_disciple,
                  nwn1::ability_constitution, 20),
        2);

    auto req = nwk::rules().classes.get_requirement(nwn1::class_type_dwarven_defender);
    EXPECT_TRUE(req);
    EXPECT_EQ(req->main.size(), 4);
    nw::kernel::unload_module();
}

TEST(KernelRules, MasterFeats)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);

    int result = nw::kernel::sum_master_feats<int>(
        obj, nwn1::base_item_scimitar,
        nwn1::mfeat_weapon_focus, nwn1::mfeat_weapon_focus_epic);

    EXPECT_EQ(result, 3);

    int result2 = nw::kernel::sum_master_feats<int>(
        obj, nwn1::base_item_gloves,
        nwn1::mfeat_weapon_focus, nwn1::mfeat_weapon_focus_epic);

    EXPECT_EQ(result2, 0);

    nw::kernel::unload_module();
}

TEST(KernelRules, SpellSchools)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto& spellschools = nwk::rules().spellschools;
    EXPECT_GT(spellschools.entries.size(), 1);
    EXPECT_EQ(spellschools.entries[1].letter, "A");

    nw::kernel::unload_module();
}

TEST(KernelRules, Traps)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto& traps = nwk::rules().traps;
    EXPECT_GT(traps.entries.size(), 1);
    EXPECT_EQ(traps.entries[1].script, "nw_t1_spikeavgc");
    EXPECT_EQ(traps.entries[2].name, 6848);

    nw::kernel::unload_module();
}

TEST(KernelRules, Placealbes)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto& placeables = nwk::rules().placeables;
    EXPECT_GT(placeables.entries.size(), 1);
    EXPECT_EQ(placeables.entries[1].model, "plc_a02");
    EXPECT_TRUE(placeables.entries[2].static_);

    nw::kernel::unload_module();
}
