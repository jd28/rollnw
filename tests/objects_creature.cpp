#include <gtest/gtest.h>

#include <nw/functions.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/Archives.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/combat.hpp>
#include <nwn1/constants.hpp>
#include <nwn1/effects.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Creature, GffDeserialize)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj1 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj1);
    EXPECT_TRUE(nwk::objects().valid(obj1->handle()));

    EXPECT_EQ(obj1->common.resref, "nw_chicken");
    EXPECT_EQ(obj1->stats.get_ability_score(nwn1::ability_dexterity), 7);
    EXPECT_EQ(obj1->scripts.on_attacked, "nw_c2_default5");
    EXPECT_EQ(obj1->appearance.id, 31);
    EXPECT_EQ(obj1->gender, 1);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj2);

    EXPECT_EQ(obj2->common.resref, "pl_agent_001");
    EXPECT_EQ(obj2->stats.get_ability_score(nwn1::ability_dexterity), 13);
    EXPECT_EQ(obj2->scripts.on_attacked, "mon_ai_5attacked");
    EXPECT_EQ(obj2->appearance.id, 6);
    EXPECT_EQ(obj2->appearance.body_parts.shin_left, 1);
    EXPECT_EQ(obj2->soundset, 171);
    EXPECT_TRUE(std::get<nw::Item*>(obj2->equipment.equips[1]));
    EXPECT_EQ(obj2->combat_info.ac_natural_bonus, 0);
    EXPECT_EQ(obj2->combat_info.special_abilities.size(), 1);
    EXPECT_EQ(obj2->combat_info.special_abilities[0].spell, 120);

    nwk::unload_module();
}

TEST(Creature, FeatsAddRemove)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->stats.feats().size(), 37);
    EXPECT_TRUE(ent->stats.has_feat(ent->stats.feats()[20]));
    EXPECT_FALSE(ent->stats.add_feat(ent->stats.feats()[20]));

    nwk::unload_module();
}

TEST(Creature, FeatSearch)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    EXPECT_TRUE(nw::knows_feat(ent, nwn1::feat_epic_toughness_2));
    EXPECT_FALSE(nw::knows_feat(ent, nwn1::feat_epic_toughness_1));

    auto [feat, nth] = nw::has_feat_successor(ent, nwn1::feat_epic_toughness_2);
    EXPECT_TRUE(nth);
    EXPECT_EQ(feat, nwn1::feat_epic_toughness_4);

    auto [feat2, nth2] = nw::has_feat_successor(ent, nwn1::feat_epic_great_wisdom_1);
    EXPECT_EQ(nth2, 0);

    auto feat3 = nw::highest_feat_in_range(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    EXPECT_EQ(feat3, nwn1::feat_epic_toughness_4);

    auto n = nw::count_feats_in_range(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    EXPECT_EQ(n, 3); // char doesn't have et1.

    nwk::unload_module();
}

TEST(Creature, Ability)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);
    EXPECT_TRUE(obj->instantiate());

    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength), 40);
    EXPECT_EQ(nwn1::get_ability_modifier(obj, nwn1::ability_strength), 15);

    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_dexterity), 17);
    EXPECT_EQ(nwn1::get_ability_modifier(obj, nwn1::ability_dexterity), 3);

    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength, false), 40);
    obj->stats.add_feat(nwn1::feat_epic_great_strength_1);
    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength, false), 41);
    obj->stats.add_feat(nwn1::feat_epic_great_strength_2);
    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength, false), 42);

    auto eff = nwn1::effect_ability_modifier(nwn1::ability_strength, 5);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_TRUE(obj->effects().size() > 0);
    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength, false), 47);

    auto eff2 = nwn1::effect_ability_modifier(nwn1::ability_strength, -3);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength, false), 44);

    // Belt of Cloud Giant Strength
    auto item = nwk::objects().load<nw::Item>("x2_it_mbelt001"sv);
    EXPECT_TRUE(item);
    EXPECT_TRUE(nwn1::equip_item(obj, item, nw::EquipIndex::belt));
    EXPECT_TRUE(obj->effects().size() > 1);
    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength, false), 52);
    EXPECT_TRUE(nwn1::unequip_item(obj, nw::EquipIndex::belt));
    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength, false), 44);

    // Class stat gain
    auto obj2 = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/sorcrdd.utc"));
    EXPECT_TRUE(obj2);
    EXPECT_TRUE(obj2->instantiate());

    EXPECT_EQ(obj2->stats.get_ability_score(nwn1::ability_strength), 15);
    EXPECT_EQ(nwn1::get_ability_score(obj2, nwn1::ability_strength), 23);

    EXPECT_EQ(obj2->stats.get_ability_score(nwn1::ability_constitution), 14);
    EXPECT_EQ(nwn1::get_ability_score(obj2, nwn1::ability_constitution), 14); // This is an elf!

    nwk::unload_module();
}

TEST(Creature, Skills)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);

    EXPECT_EQ(nwn1::get_skill_rank(obj, nwn1::skill_discipline, nullptr, true), 40);
    obj->stats.add_feat(nwn1::feat_skill_focus_discipline);
    EXPECT_EQ(nwn1::get_skill_rank(obj, nwn1::skill_discipline), 61);
    obj->stats.add_feat(nwn1::feat_epic_skill_focus_discipline);
    EXPECT_EQ(nwn1::get_skill_rank(obj, nwn1::skill_discipline), 71);

    auto eff = nwn1::effect_skill_modifier(nwn1::skill_discipline, 5);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_EQ(nwn1::get_skill_rank(obj, nwn1::skill_discipline), 76);

    auto eff2 = nwn1::effect_ability_modifier(nwn1::ability_strength, 5);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    EXPECT_EQ(nwn1::get_skill_rank(obj, nwn1::skill_discipline), 78);

    nwk::unload_module();
}

TEST(Creature, ArmorClass)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);
    EXPECT_FALSE(obj->hasted);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 14);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 14);

    auto eff1 = nwn1::effect_haste();
    nw::apply_effect(obj, eff1);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 18);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 18);

    auto eff2 = nwn1::effect_armor_class_modifier(nwn1::ac_dodge, 4);
    nw::apply_effect(obj, eff2);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 22);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 22);

    auto eff3 = nwn1::effect_armor_class_modifier(nwn1::ac_shield, 2);
    nw::apply_effect(obj, eff3);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 24);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 22);

    auto eff4 = nwn1::effect_armor_class_modifier(nwn1::ac_natural, 2);
    nw::apply_effect(obj, eff4);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 26);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 22);

    auto eff5 = nwn1::effect_armor_class_modifier(nwn1::ac_deflection, 2);
    nw::apply_effect(obj, eff5);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 28);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 24);

    auto eff6 = nwn1::effect_armor_class_modifier(nwn1::ac_natural, 1);
    nw::apply_effect(obj, eff6);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 28);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 24);

    auto eff7 = nwn1::effect_armor_class_modifier(nwn1::ac_dodge, 1);
    nw::apply_effect(obj, eff7);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 29);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 25);

    auto eff8 = nwn1::effect_armor_class_modifier(nwn1::ac_dodge, -1);
    nw::apply_effect(obj, eff8);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, false), 28);
    EXPECT_EQ(nwn1::calculate_ac_versus(obj, nullptr, true), 24);

    nwk::unload_module();
}

TEST(Creature, Attack)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);
    EXPECT_TRUE(nw::has_effect_applied(obj, nwn1::effect_type_damage_increase));

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj2);

    for (size_t i = 0; i < 100; ++i) {
        auto data1 = nwn1::resolve_attack(obj, obj2);
        EXPECT_TRUE(data1);
        EXPECT_EQ(data1->type, nwn1::attack_type_unarmed);
        if (nw::is_attack_type_hit(data1->result)) {
            EXPECT_GT(data1->damage_total, 0);
        } else {
            EXPECT_EQ(data1->damage_total, 0);
        }
    }

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj3);

    for (size_t i = 0; i < 100; ++i) {
        auto data1 = nwn1::resolve_attack(obj3, obj);
        EXPECT_TRUE(data1);
        EXPECT_EQ(data1->type, nwn1::attack_type_onhand);
        if (nw::is_attack_type_hit(data1->result)) {
            EXPECT_GT(data1->damage_total, 0);
        } else {
            EXPECT_EQ(data1->damage_total, 0);
        }
    }

    auto obj4 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/rangerdexranged.utc"));
    EXPECT_TRUE(obj4);

    auto vs4 = nwk::objects().load<nw::Creature>("nw_drggreen003"sv);
    EXPECT_TRUE(vs4);
    for (size_t i = 0; i < 100; ++i) {
        auto data1 = nwn1::resolve_attack(obj4, vs4);
        EXPECT_TRUE(data1);
        EXPECT_EQ(data1->type, nwn1::attack_type_onhand);
        if (nw::is_attack_type_hit(data1->result)) {
            EXPECT_GT(data1->damage_total, 0);
        } else {
            EXPECT_EQ(data1->damage_total, 0);
        }
    }
}

TEST(Creature, BaseAttackBonus)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);

    EXPECT_EQ(27, nwn1::base_attack_bonus(obj));
    EXPECT_EQ(47, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));

    EXPECT_FALSE(obj->stats.has_feat(nwn1::feat_weapon_focus_unarmed));
    EXPECT_FALSE(obj->stats.has_feat(nwn1::feat_epic_weapon_focus_unarmed));
    obj->stats.add_feat(nwn1::feat_weapon_focus_unarmed);
    obj->stats.add_feat(nwn1::feat_epic_weapon_focus_unarmed);

    EXPECT_EQ(50, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));
    auto eff1 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 2);
    EXPECT_TRUE(nw::apply_effect(obj, eff1));
    EXPECT_EQ(52, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));
    auto eff2 = nwn1::effect_attack_modifier(nwn1::attack_type_unarmed, 3);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    EXPECT_EQ(55, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));

    nwk::unload_module();
}

TEST(Creature, AttackBonus)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);

    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(vs);

    int test = 0;
    auto adder = [&test](int val) { test += val; };
    nw::resolve_effects_of<int>(
        obj->effects().begin(),
        obj->effects().end(),
        nwn1::effect_type_attack_increase,
        *nwn1::attack_type_onhand,
        nw::Versus{},
        adder, &nw::effect_extract_int0);
    EXPECT_EQ(test, 2);

    EXPECT_EQ(obj->levels.level(), 38);
    EXPECT_EQ(obj->levels.level_by_class(nwn1::class_type_fighter), 10);
    EXPECT_EQ(obj->levels.level_by_class(nwn1::class_type_weapon_master), 28);
    EXPECT_EQ(nwn1::get_ability_score(obj, nwn1::ability_strength), 16);
    EXPECT_EQ(nwn1::get_ability_modifier(obj, nwn1::ability_strength), 3);
    EXPECT_EQ(obj->combat_info.size_ab_modifier, 1);
    EXPECT_EQ(29, nwn1::base_attack_bonus(obj));
    EXPECT_EQ(45, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_info.combat_mode = nwn1::combat_mode_power_attack;
    EXPECT_EQ(40, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_info.combat_mode = nwn1::combat_mode_improved_power_attack;
    EXPECT_EQ(35, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_info.combat_mode = nwn1::combat_mode_expertise;
    EXPECT_EQ(40, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_info.combat_mode = nwn1::combat_mode_improved_expertise;
    EXPECT_EQ(35, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_info.combat_mode = nw::CombatMode::invalid();
    EXPECT_EQ(45, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->stats.add_feat(nwn1::feat_epic_prowess);
    EXPECT_EQ(46, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_info.target_state = nw::TargetState::flanked;
    EXPECT_EQ(48, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand, vs));
    obj->combat_info.target_state = nw::TargetState::none;

    // Good aim
    EXPECT_EQ(nwn1::get_ability_modifier(obj, nwn1::ability_dexterity), 2);
    auto sling = nwk::objects().load<nw::Item>("nw_wbwsl001"sv);
    EXPECT_TRUE(sling);
    nwn1::equip_item(obj, sling, nw::EquipIndex::righthand);
    EXPECT_EQ(34, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    EXPECT_TRUE(obj->stats.has_feat(nwn1::feat_good_aim));
    auto dart = nwk::objects().load<nw::Item>("nw_wthdt001"sv);
    EXPECT_TRUE(dart);
    nwn1::equip_item(obj, dart, nw::EquipIndex::righthand);
    EXPECT_EQ(34, nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    // Zen Archery
    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/lentiane.utc"));
    EXPECT_TRUE(obj2);

    EXPECT_TRUE(nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand));
    EXPECT_EQ(nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand)->baseitem, nwn1::base_item_shortbow);
    EXPECT_TRUE(nwn1::is_ranged_weapon(nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand)));
    EXPECT_TRUE(obj2->stats.has_feat(nwn1::feat_zen_archery));
    EXPECT_EQ(nwn1::get_ability_modifier(obj2, nwn1::ability_wisdom), 10);
    EXPECT_EQ(nwn1::get_ability_modifier(obj2, nwn1::ability_dexterity), 3);
    EXPECT_EQ(26, nwn1::base_attack_bonus(obj2));
    EXPECT_EQ(36, nwn1::resolve_attack_bonus(obj2, nwn1::attack_type_onhand));

    // Dex Archery
    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/rangerdexranged.utc"));
    EXPECT_TRUE(obj3);

    auto vs1 = nwk::objects().load<nw::Creature>("nw_drggreen003"sv);
    EXPECT_TRUE(vs1);

    EXPECT_TRUE(!obj3->stats.has_feat(nwn1::feat_zen_archery));
    EXPECT_TRUE(obj3->stats.has_feat(nwn1::feat_epic_bane_of_enemies));
    EXPECT_EQ(obj3->combat_info.combat_mode, nw::CombatMode::invalid());
    EXPECT_TRUE(nwn1::get_equipped_item(obj3, nw::EquipIndex::righthand));
    EXPECT_EQ(nwn1::get_equipped_item(obj3, nw::EquipIndex::righthand)->baseitem, nwn1::base_item_longbow);
    EXPECT_TRUE(nwn1::is_ranged_weapon(nwn1::get_equipped_item(obj3, nw::EquipIndex::righthand)));
    EXPECT_EQ(nwn1::get_ability_modifier(obj3, nwn1::ability_strength), 2);
    EXPECT_EQ(nwn1::get_ability_modifier(obj3, nwn1::ability_dexterity), 8);
    EXPECT_EQ(30, nwn1::base_attack_bonus(obj3));
    EXPECT_EQ(47, nwn1::resolve_attack_bonus(obj3, nwn1::attack_type_onhand));
    // Bane of enemies
    EXPECT_EQ(49, nwn1::resolve_attack_bonus(obj3, nwn1::attack_type_onhand, vs1));

    auto eff1 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 3);
    eff1->set_versus(vs1->versus_me());
    nw::apply_effect(obj3, eff1);
    auto eff2 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 3);
    eff2->set_versus({nwn1::racial_type_halfling});
    nw::apply_effect(obj3, eff2);
    EXPECT_EQ(52, nwn1::resolve_attack_bonus(obj3, nwn1::attack_type_onhand, vs1));

    // Dex Weapon Finesse
    auto obj4 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/dexweapfin.utc"));
    EXPECT_TRUE(obj4);

    EXPECT_TRUE(obj4->stats.has_feat(nwn1::feat_weapon_finesse));
    EXPECT_EQ(obj4->combat_info.combat_mode, nw::CombatMode::invalid());
    EXPECT_TRUE(nwn1::get_equipped_item(obj4, nw::EquipIndex::righthand));
    EXPECT_EQ(nwn1::get_equipped_item(obj4, nw::EquipIndex::righthand)->baseitem, nwn1::base_item_dagger);
    EXPECT_EQ(nwn1::get_ability_modifier(obj4, nwn1::ability_strength), 1);
    EXPECT_EQ(nwn1::get_ability_modifier(obj4, nwn1::ability_dexterity), 11);
    EXPECT_EQ(25, nwn1::base_attack_bonus(obj4));
    EXPECT_EQ(40, nwn1::resolve_attack_bonus(obj4, nwn1::attack_type_onhand));

    // Dual Wield
    auto rh = nwn1::get_equipped_item(obj4, nw::EquipIndex::righthand);
    EXPECT_TRUE(nwn1::equip_item(obj4, rh, nw::EquipIndex::lefthand));
    auto [on, off] = nwn1::resolve_number_of_attacks(obj4);
    EXPECT_EQ(on, 4);
    EXPECT_EQ(off, 1);

    EXPECT_TRUE(obj4->stats.add_feat(nwn1::feat_improved_two_weapon_fighting));
    auto [on2, off2] = nwn1::resolve_number_of_attacks(obj4);
    EXPECT_EQ(on2, 4);
    EXPECT_EQ(off2, 2);

    EXPECT_EQ(38, nwn1::resolve_attack_bonus(obj4, nwn1::attack_type_onhand));
    EXPECT_EQ(38, nwn1::resolve_attack_bonus(obj4, nwn1::attack_type_offhand));

    nwk::unload_module();
}

TEST(Creature, AttackRoll)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);

    auto target = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(target);

    EXPECT_EQ(nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand, target), 45);
    EXPECT_EQ(nwn1::calculate_ac_versus(target, obj, false), 11);

    for (size_t i = 0; i < 100; ++i) {
        auto result = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, target);
        EXPECT_TRUE((result == nw::AttackResult::miss_by_auto_fail || nw::is_attack_type_hit(result)));
    }

    auto eff = nwn1::effect_concealment(100);
    EXPECT_TRUE(eff);
    EXPECT_TRUE(nw::apply_effect(target, eff));
    for (size_t i = 0; i < 100; ++i) {
        auto result = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, target);
        EXPECT_TRUE((result == nw::AttackResult::miss_by_auto_fail || result == nw::AttackResult::miss_by_concealment));
    }
    nw::remove_effect(target, eff);

    auto eff2 = nwn1::effect_miss_chance(100);
    EXPECT_TRUE(eff2);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    for (size_t i = 0; i < 100; ++i) {
        auto result = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, target);
        EXPECT_TRUE((result == nw::AttackResult::miss_by_auto_fail || result == nw::AttackResult::miss_by_miss_chance));
    }
}

TEST(Creature, AttackType)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);
    obj->combat_info.attacks_onhand = 4;
    EXPECT_EQ(nwn1::resolve_attack_type(obj), nwn1::attack_type_onhand);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj2);
    obj2->combat_info.attacks_onhand = 1;
    EXPECT_EQ(nwn1::resolve_attack_type(obj2), nwn1::attack_type_unarmed);
}

TEST(Creature, Concealment)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj);
    obj->stats.add_feat(nwn1::feat_epic_self_concealment_30);
    auto [c1, o1] = nwn1::resolve_concealment(obj, obj);
    EXPECT_EQ(c1, 30);
    EXPECT_FALSE(o1);

    auto eff = nwn1::effect_concealment(25);
    EXPECT_TRUE(eff);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_TRUE(obj->effects().size() > 0);
    EXPECT_EQ(*obj->effects().begin()->type, *nwn1::effect_type_concealment);
    EXPECT_TRUE(nwn1::has_effect_type_applied(obj, nwn1::effect_type_concealment));
    auto [c2, o2] = nwn1::resolve_concealment(obj, obj);
    EXPECT_EQ(c2, 30);
    EXPECT_FALSE(o2);

    auto eff2 = nwn1::effect_miss_chance(35);
    EXPECT_TRUE(eff2);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    EXPECT_GT(obj->effects().size(), 1);
    EXPECT_TRUE(nwn1::has_effect_type_applied(obj, nwn1::effect_type_miss_chance));
    auto [c3, o3] = nwn1::resolve_concealment(obj, obj);
    EXPECT_EQ(c3, 35);
    EXPECT_TRUE(o3);
}

TEST(Creature, CriticalHitMultiplier)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);

    int multiplier = nwn1::resolve_critical_multiplier(obj, nwn1::attack_type_onhand);
    EXPECT_EQ(multiplier, 3);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj2);
    int multiplier2 = nwn1::resolve_critical_multiplier(obj2, nwn1::attack_type_onhand);
    EXPECT_EQ(multiplier2, 2);
}

TEST(Creature, CriticalHitThreat)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);

    int threat = nwn1::resolve_critical_threat(obj, nwn1::attack_type_onhand);
    EXPECT_EQ(threat, 11);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj2);
    int threat2 = nwn1::resolve_critical_threat(obj2, nwn1::attack_type_onhand);
    EXPECT_EQ(threat2, 1);
    obj2->stats.add_feat(nwn1::feat_improved_critical_unarmed);
    threat2 = nwn1::resolve_critical_threat(obj2, nwn1::attack_type_onhand);
    EXPECT_EQ(threat2, 2);
}

TEST(Creature, DamageBase)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);
    EXPECT_TRUE(obj->stats.add_feat(nwn1::feat_weapon_specialization_unarmed));
    auto dice1 = nwn1::resolve_unarmed_damage(obj);
    EXPECT_EQ(dice1.dice, 1);
    EXPECT_EQ(dice1.sides, 20);
    EXPECT_EQ(dice1.bonus, 4);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj2);
    EXPECT_TRUE(obj2->stats.add_feat(nwn1::feat_epic_weapon_specialization_scimitar));
    auto weapon2 = nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand);
    EXPECT_TRUE(weapon2);
    auto dice2 = nwn1::resolve_weapon_damage(obj2, weapon2->baseitem);
    EXPECT_EQ(dice2.dice, 1);
    EXPECT_EQ(dice2.sides, 6);
    EXPECT_EQ(dice2.bonus, 8);

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj3);
    auto dice3 = nwn1::resolve_unarmed_damage(obj3);
    EXPECT_EQ(dice3.dice, 1);
    EXPECT_EQ(dice3.sides, 2);
}

TEST(Creature, DamageWeaponFlags)
{
    auto obj1 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj1);
    auto weapon1 = nwn1::get_equipped_item(obj1, nw::EquipIndex::righthand);
    EXPECT_TRUE(weapon1);
    EXPECT_TRUE(nwn1::resolve_weapon_damage_flags(weapon1).test(nwn1::damage_type_slashing));

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj2);
    auto weapon2 = nwn1::get_equipped_item(obj2, nw::EquipIndex::arms);
    EXPECT_TRUE(weapon2);
    EXPECT_TRUE(nwn1::resolve_weapon_damage_flags(weapon2).test(nwn1::damage_type_bludgeoning));
}

TEST(Creature, DamageImmunity)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);

    auto eff = nwn1::effect_damage_immunity(nwn1::damage_type_bludgeoning, 10);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_EQ(nwn1::resolve_damage_immunity(obj, nwn1::damage_type_bludgeoning), 10);

    auto eff2 = nwn1::effect_damage_immunity(nwn1::damage_type_bludgeoning, -3);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    EXPECT_EQ(nwn1::resolve_damage_immunity(obj, nwn1::damage_type_bludgeoning), 7);

    // Fake RDD.
    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj2);
    obj2->levels.entries[0].id = nwn1::class_type_dragon_disciple;
    obj2->levels.entries[0].level = 20;
    EXPECT_EQ(nwn1::resolve_damage_immunity(obj2, nwn1::damage_type_fire), 100);
}

TEST(Creature, DamageReduction)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);

    auto eff = nwn1::effect_damage_reduction(30, 2);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 1).first, 30);
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 3).first, 0);

    auto eff2 = nwn1::effect_damage_reduction(20, 4, 100);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    auto eff3 = nwn1::effect_damage_reduction(20, 4, 50);
    EXPECT_TRUE(nw::apply_effect(obj, eff3));
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 1).first, 30);
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 3).first, 20);
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj, 4).first, 0);

    // Fake DD.
    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj2);
    obj2->levels.entries[0].id = nwn1::class_type_dwarven_defender;
    obj2->levels.entries[0].level = 20;
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj2, 1).first, 12);

    // Fake Barb.
    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj3);
    obj3->levels.entries[0].id = nwn1::class_type_barbarian;
    obj3->levels.entries[0].level = 20;
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj3, 1).first, 4);

    obj3->stats.add_feat(nwn1::feat_epic_damage_reduction_6);
    EXPECT_EQ(nwn1::resolve_damage_reduction(obj3, 1).first, 10);
}

TEST(Creature, DamageResistance)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj);

    auto eff = nwn1::effect_damage_resistance(nwn1::damage_type_acid, 10);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_EQ(nwn1::resolve_damage_resistance(obj, nwn1::damage_type_acid).first, 10);
    EXPECT_EQ(nwn1::resolve_damage_resistance(obj, nwn1::damage_type_fire).first, 0);

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj3);
    obj3->stats.add_feat(nwn1::feat_epic_energy_resistance_sonic_3);
    EXPECT_EQ(nwn1::resolve_damage_resistance(obj3, nwn1::damage_type_sonic).first, 30);
}

TEST(Creature, Hitpoints)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj);

    EXPECT_EQ(nwn1::get_current_hitpoints(obj), 4);
    EXPECT_EQ(nwn1::get_max_hitpoints(obj), 4);
    obj->stats.add_feat(nwn1::feat_toughness);
    EXPECT_EQ(nwn1::get_max_hitpoints(obj), 5);
    obj->stats.add_feat(nwn1::feat_epic_toughness_5);
    EXPECT_EQ(nwn1::get_max_hitpoints(obj), 105);

    auto eff = nwn1::effect_hitpoints_temporary(10);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_EQ(nwn1::get_max_hitpoints(obj), 115);
    EXPECT_TRUE(nw::remove_effect(obj, eff));
    EXPECT_EQ(nwn1::get_max_hitpoints(obj), 105);
}

TEST(Creature, IterationPenalty)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);

    EXPECT_EQ(nwn1::resolve_iteration_penalty(obj, nwn1::attack_type_onhand), 0);

    obj->combat_info.attack_current = 3;
    EXPECT_EQ(nwn1::resolve_iteration_penalty(obj, nwn1::attack_type_onhand), 15);

    // Note the guy is hasted
    obj->combat_info.attacks_onhand = 1;
    EXPECT_EQ(nwn1::resolve_iteration_penalty(obj, nwn1::attack_type_offhand), 5);
}

TEST(Creature, SavingThrows)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);
    EXPECT_EQ(nwn1::saving_throw(obj, nwn1::saving_throw_fort), 20);
    EXPECT_EQ(nwn1::saving_throw(obj, nwn1::saving_throw_reflex), 21);
    EXPECT_EQ(nwn1::saving_throw(obj, nwn1::saving_throw_will), 13);

    auto eff = nwn1::effect_save_modifier(nwn1::saving_throw_fort, 5);
    EXPECT_TRUE(eff);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_EQ(nwn1::saving_throw(obj, nwn1::saving_throw_fort), 25);

    auto eff2 = nwn1::effect_save_modifier(nwn1::saving_throw_fort, -2);
    EXPECT_TRUE(eff2);
    EXPECT_TRUE(nw::apply_effect(obj, eff2));
    EXPECT_EQ(nwn1::saving_throw(obj, nwn1::saving_throw_fort), 23);
}

TEST(Creature, TargetState)
{
    nw::TargetState state = nw::TargetState::none;
    state = nwn1::resolve_target_state(nullptr, nullptr);
    EXPECT_EQ(state, nw::TargetState::none);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);
    state = nwn1::resolve_target_state(obj, nullptr);
    EXPECT_EQ(state, nw::TargetState::none);

    auto target = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(target);

    state = nwn1::resolve_target_state(obj, target);
    EXPECT_TRUE(to_bool(state & nw::TargetState::flanked));

    target->combat_info.target = obj;
    state = nwn1::resolve_target_state(obj, target);
    EXPECT_FALSE(to_bool(state & nw::TargetState::flanked));

    target->combat_info.target = nullptr;
    obj->combat_info.target_distance_sq = 200.0f;
    state = nwn1::resolve_target_state(obj, target);
    EXPECT_FALSE(to_bool(state & nw::TargetState::flanked));
}

TEST(Creature, WeaponPower)
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    EXPECT_TRUE(obj);
    EXPECT_EQ(nwn1::resolve_weapon_power(obj, nwn1::get_equipped_item(obj, nw::EquipIndex::righthand)), 2);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    EXPECT_TRUE(obj2);
    EXPECT_EQ(nwn1::resolve_weapon_power(obj2, nullptr), 0);

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(obj3);
    EXPECT_EQ(nwn1::resolve_weapon_power(obj3, nwn1::get_equipped_item(obj3, nw::EquipIndex::arms)), 5);

    obj3->stats.add_feat(nwn1::feat_ki_strike);
    EXPECT_EQ(nwn1::resolve_weapon_power(obj3, nullptr), 1);
}

TEST(Creature, JsonSerialization)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Creature::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/pl_agent_001.utc.json"};
    f << std::setw(4) << j;

    nwk::unload_module();
}

TEST(Creature, JsonRoundTrip)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Creature::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Creature>();
    EXPECT_TRUE(ent2);
    nw::Creature::deserialize(ent2, j, nw::SerializationProfile::blueprint);

    nlohmann::json j2;
    nw::Creature::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);

    nwk::unload_module();
}

TEST(Creature, GffRoundTrip)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    EXPECT_TRUE(ent);

    nw::Gff g("test_data/user/development/pl_agent_001.utc");
    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_agent_001_2.utc");

    // Note: the below will typically always fail because the toolset,
    // doesn't sort feats when it writes out the gff.
    // nw::Gff g2("tmp/pl_agent_001_2.utc");
    // EXPECT_TRUE(g2.valid());
    // EXPECT_TRUE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

    EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);

    nwk::unload_module();
}

TEST(Creature, Equips)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/test_creature.utc"));
    EXPECT_TRUE(obj);
    EXPECT_TRUE(obj->instantiate());

    auto item = nwk::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_TRUE(item);

    EXPECT_TRUE(nwn1::equip_item(obj, item, nw::EquipIndex::chest));
    EXPECT_TRUE(nwn1::get_equipped_item(obj, nw::EquipIndex::chest));

    auto item1 = nwn1::unequip_item(obj, nw::EquipIndex::chest);
    EXPECT_TRUE(item1);
    EXPECT_FALSE(nwn1::get_equipped_item(obj, nw::EquipIndex::chest));

    EXPECT_FALSE(nwn1::equip_item(obj, item, nw::EquipIndex::head));

    auto item2 = nwn1::unequip_item(obj, nw::EquipIndex::head);
    EXPECT_FALSE(item2);

    auto boots_of_speed = nwk::objects().load<nw::Item>("nw_it_mboots005"sv);
    EXPECT_TRUE(boots_of_speed);
    EXPECT_TRUE(nwn1::equip_item(obj, boots_of_speed, nw::EquipIndex::boots));
    EXPECT_TRUE(obj->hasted);
    auto boots_of_speed2 = nwn1::unequip_item(obj, nw::EquipIndex::boots);
    EXPECT_TRUE(boots_of_speed2);
    EXPECT_FALSE(obj->hasted);

    nwk::unload_module();
}

TEST(Creature, EffectsApplyRemove)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/test_creature.utc"));
    EXPECT_TRUE(obj);
    EXPECT_TRUE(obj->instantiate());

    auto eff = nwn1::effect_haste();
    EXPECT_TRUE(obj->effects().add(eff));
    EXPECT_TRUE(nw::has_effect_applied(obj, nwn1::effect_type_haste));
    EXPECT_TRUE(obj->effects().size());
    EXPECT_TRUE(obj->effects().remove(eff));
    EXPECT_FALSE(nw::has_effect_applied(obj, nwn1::effect_type_haste));
    EXPECT_EQ(obj->effects().size(), 0);
    EXPECT_FALSE(obj->effects().remove(nullptr));

    nwk::unload_module();
}
