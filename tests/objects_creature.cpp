#include <catch2/catch_all.hpp>

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

TEST_CASE("creature: load nw_chicken", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(ent);
    REQUIRE(nwk::objects().valid(ent->handle()));

    REQUIRE(ent->common.resref == "nw_chicken");
    REQUIRE(ent->stats.get_ability_score(nwn1::ability_dexterity) == 7);
    REQUIRE(ent->scripts.on_attacked == "nw_c2_default5");
    REQUIRE(ent->appearance.id == 31);
    REQUIRE(ent->gender == 1);

    nwk::unload_module();
}

TEST_CASE("creature: load pl_agent_001", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "pl_agent_001");
    REQUIRE(ent->stats.get_ability_score(nwn1::ability_dexterity) == 13);
    REQUIRE(ent->scripts.on_attacked == "mon_ai_5attacked");
    REQUIRE(ent->appearance.id == 6);
    REQUIRE(ent->appearance.body_parts.shin_left == 1);
    REQUIRE(ent->soundset == 171);
    REQUIRE(std::get<nw::Item*>(ent->equipment.equips[1]));
    REQUIRE(ent->combat_info.ac_natural_bonus == 0);
    REQUIRE(ent->combat_info.special_abilities.size() == 1);
    REQUIRE(ent->combat_info.special_abilities[0].spell == 120);

    nwk::unload_module();
}

TEST_CASE("creature: add/has/remove_feat", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(ent->stats.feats().size() == 37);
    REQUIRE(ent->stats.has_feat(ent->stats.feats()[20]));
    REQUIRE_FALSE(ent->stats.add_feat(ent->stats.feats()[20]));

    nwk::unload_module();
}

TEST_CASE("creature: feat search", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(nw::knows_feat(ent, nwn1::feat_epic_toughness_2));
    REQUIRE_FALSE(nw::knows_feat(ent, nwn1::feat_epic_toughness_1));

    auto [feat, nth] = nw::has_feat_successor(ent, nwn1::feat_epic_toughness_2);
    REQUIRE(nth);
    REQUIRE(feat == nwn1::feat_epic_toughness_4);

    auto [feat2, nth2] = nw::has_feat_successor(ent, nwn1::feat_epic_great_wisdom_1);
    REQUIRE(nth2 == 0);

    auto feat3 = nw::highest_feat_in_range(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    REQUIRE(feat3 == nwn1::feat_epic_toughness_4);

    auto n = nw::count_feats_in_range(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    REQUIRE(n == 3); // char doesn't have et1.

    nwk::unload_module();
}

TEST_CASE("creature: ability ", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);
    REQUIRE(obj->instantiate());

    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength) == 40);
    REQUIRE(nwn1::get_ability_modifier(obj, nwn1::ability_strength) == 15);

    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_dexterity) == 17);
    REQUIRE(nwn1::get_ability_modifier(obj, nwn1::ability_dexterity) == 3);

    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 40);
    obj->stats.add_feat(nwn1::feat_epic_great_strength_1);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 41);
    obj->stats.add_feat(nwn1::feat_epic_great_strength_2);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 42);

    auto eff = nwn1::effect_ability_modifier(nwn1::ability_strength, 5);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(obj->effects().size() > 0);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 47);

    auto eff2 = nwn1::effect_ability_modifier(nwn1::ability_strength, -3);
    REQUIRE(nw::apply_effect(obj, eff2));
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 44);

    // Belt of Cloud Giant Strength
    auto item = nwk::objects().load<nw::Item>("x2_it_mbelt001"sv);
    REQUIRE(item);
    REQUIRE(nwn1::equip_item(obj, item, nw::EquipIndex::belt));
    REQUIRE(obj->effects().size() > 1);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 52);
    REQUIRE(nwn1::unequip_item(obj, nw::EquipIndex::belt));
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 44);

    // Class stat gain
    auto obj2 = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/sorcrdd.utc"));
    REQUIRE(obj2);
    REQUIRE(obj2->instantiate());

    REQUIRE(obj2->stats.get_ability_score(nwn1::ability_strength) == 15);
    REQUIRE(nwn1::get_ability_score(obj2, nwn1::ability_strength) == 23);

    REQUIRE(obj2->stats.get_ability_score(nwn1::ability_constitution) == 14);
    REQUIRE(nwn1::get_ability_score(obj2, nwn1::ability_constitution) == 14); // This is an elf!

    nwk::unload_module();
}

TEST_CASE("creature: skills ", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);

    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline, nullptr, true) == 40);
    obj->stats.add_feat(nwn1::feat_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline) == 61);
    obj->stats.add_feat(nwn1::feat_epic_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline) == 71);

    auto eff = nwn1::effect_skill_modifier(nwn1::skill_discipline, 5);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline) == 76);

    auto eff2 = nwn1::effect_ability_modifier(nwn1::ability_strength, 5);
    REQUIRE(nw::apply_effect(obj, eff2));
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline) == 78);

    nwk::unload_module();
}

TEST_CASE("creature: armor class", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);
    REQUIRE_FALSE(obj->hasted);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 14);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 14);

    auto eff1 = nwn1::effect_haste();
    nw::apply_effect(obj, eff1);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 18);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 18);

    auto eff2 = nwn1::effect_armor_class_modifier(nwn1::ac_dodge, 4);
    nw::apply_effect(obj, eff2);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 22);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 22);

    auto eff3 = nwn1::effect_armor_class_modifier(nwn1::ac_shield, 2);
    nw::apply_effect(obj, eff3);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 24);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 22);

    auto eff4 = nwn1::effect_armor_class_modifier(nwn1::ac_natural, 2);
    nw::apply_effect(obj, eff4);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 26);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 22);

    auto eff5 = nwn1::effect_armor_class_modifier(nwn1::ac_deflection, 2);
    nw::apply_effect(obj, eff5);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 28);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 24);

    auto eff6 = nwn1::effect_armor_class_modifier(nwn1::ac_natural, 1);
    nw::apply_effect(obj, eff6);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 28);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 24);

    auto eff7 = nwn1::effect_armor_class_modifier(nwn1::ac_dodge, 1);
    nw::apply_effect(obj, eff7);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 29);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 25);

    auto eff8 = nwn1::effect_armor_class_modifier(nwn1::ac_dodge, -1);
    nw::apply_effect(obj, eff8);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, false) == 28);
    REQUIRE(nwn1::calculate_ac_versus(obj, nullptr, true) == 24);

    nwk::unload_module();
}

TEST_CASE("creature: attack", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);
    REQUIRE(nw::has_effect_applied(obj, nwn1::effect_type_damage_increase));

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj2);

    for (size_t i = 0; i < 100; ++i) {
        auto data1 = nwn1::resolve_attack(obj, obj2);
        REQUIRE(data1);
        REQUIRE(data1->type == nwn1::attack_type_unarmed);
        if (nw::is_attack_type_hit(data1->result)) {
            REQUIRE(data1->damage_total > 0);
        } else {
            REQUIRE(data1->damage_total == 0);
        }
    }

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj3);

    for (size_t i = 0; i < 100; ++i) {
        auto data1 = nwn1::resolve_attack(obj3, obj);
        REQUIRE(data1);
        REQUIRE(data1->type == nwn1::attack_type_onhand);
        if (nw::is_attack_type_hit(data1->result)) {
            REQUIRE(data1->damage_total > 0);
        } else {
            REQUIRE(data1->damage_total == 0);
        }
    }

    auto obj4 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/rangerdexranged.utc"));
    REQUIRE(obj4);

    auto vs4 = nwk::objects().load<nw::Creature>("nw_drggreen003"sv);
    REQUIRE(vs4);
    for (size_t i = 0; i < 100; ++i) {
        auto data1 = nwn1::resolve_attack(obj4, vs4);
        REQUIRE(data1);
        REQUIRE(data1->type == nwn1::attack_type_onhand);
        if (nw::is_attack_type_hit(data1->result)) {
            REQUIRE(data1->damage_total > 0);
        } else {
            REQUIRE(data1->damage_total == 0);
        }
    }
}

TEST_CASE("creature: attack bonus - base", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);

    REQUIRE(27 == nwn1::base_attack_bonus(obj));
    REQUIRE(47 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));

    REQUIRE_FALSE(obj->stats.has_feat(nwn1::feat_weapon_focus_unarmed));
    REQUIRE_FALSE(obj->stats.has_feat(nwn1::feat_epic_weapon_focus_unarmed));
    obj->stats.add_feat(nwn1::feat_weapon_focus_unarmed);
    obj->stats.add_feat(nwn1::feat_epic_weapon_focus_unarmed);

    REQUIRE(50 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));
    auto eff1 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 2);
    REQUIRE(nw::apply_effect(obj, eff1));
    REQUIRE(52 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));
    auto eff2 = nwn1::effect_attack_modifier(nwn1::attack_type_unarmed, 3);
    REQUIRE(nw::apply_effect(obj, eff2));
    REQUIRE(55 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_unarmed));

    nwk::unload_module();
}

TEST_CASE("creature: attack bonus", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);

    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(vs);

    int test = 0;
    auto adder = [&test](int val) { test += val; };
    nw::resolve_effects_of<int>(
        obj->effects().begin(),
        obj->effects().end(),
        nwn1::effect_type_attack_increase,
        *nwn1::attack_type_onhand,
        nw::Versus{},
        adder, &nw::effect_extract_int0);
    REQUIRE(test == 2);

    REQUIRE(obj->levels.level() == 38);
    REQUIRE(obj->levels.level_by_class(nwn1::class_type_fighter) == 10);
    REQUIRE(obj->levels.level_by_class(nwn1::class_type_weapon_master) == 28);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength) == 16);
    REQUIRE(nwn1::get_ability_modifier(obj, nwn1::ability_strength) == 3);
    REQUIRE(obj->combat_info.size_ab_modifier == 1);
    REQUIRE(29 == nwn1::base_attack_bonus(obj));
    REQUIRE(45 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_info.combat_mode = nwn1::combat_mode_power_attack;
    REQUIRE(40 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_info.combat_mode = nwn1::combat_mode_improved_power_attack;
    REQUIRE(35 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_info.combat_mode = nwn1::combat_mode_expertise;
    REQUIRE(40 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_info.combat_mode = nwn1::combat_mode_improved_expertise;
    REQUIRE(35 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_info.combat_mode = nw::CombatMode::invalid();
    REQUIRE(45 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->stats.add_feat(nwn1::feat_epic_prowess);
    REQUIRE(46 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_info.target_state = nw::TargetState::flanked;
    REQUIRE(48 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand, vs));
    obj->combat_info.target_state = nw::TargetState::none;

    // Good aim
    REQUIRE(nwn1::get_ability_modifier(obj, nwn1::ability_dexterity) == 2);
    auto sling = nwk::objects().load<nw::Item>("nw_wbwsl001"sv);
    REQUIRE(sling);
    nwn1::equip_item(obj, sling, nw::EquipIndex::righthand);
    REQUIRE(34 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    REQUIRE(obj->stats.has_feat(nwn1::feat_good_aim));
    auto dart = nwk::objects().load<nw::Item>("nw_wthdt001"sv);
    REQUIRE(dart);
    nwn1::equip_item(obj, dart, nw::EquipIndex::righthand);
    REQUIRE(34 == nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand));

    // Zen Archery
    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/lentiane.utc"));
    REQUIRE(obj2);

    REQUIRE(nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand));
    REQUIRE(nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand)->baseitem == nwn1::base_item_shortbow);
    REQUIRE(nwn1::is_ranged_weapon(nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand)));
    REQUIRE(obj2->stats.has_feat(nwn1::feat_zen_archery));
    REQUIRE(nwn1::get_ability_modifier(obj2, nwn1::ability_wisdom) == 10);
    REQUIRE(nwn1::get_ability_modifier(obj2, nwn1::ability_dexterity) == 3);
    REQUIRE(26 == nwn1::base_attack_bonus(obj2));
    REQUIRE(36 == nwn1::resolve_attack_bonus(obj2, nwn1::attack_type_onhand));

    // Dex Archery
    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/rangerdexranged.utc"));
    REQUIRE(obj3);

    auto vs1 = nwk::objects().load<nw::Creature>("nw_drggreen003"sv);
    REQUIRE(vs1);

    REQUIRE(!obj3->stats.has_feat(nwn1::feat_zen_archery));
    REQUIRE(obj3->stats.has_feat(nwn1::feat_epic_bane_of_enemies));
    REQUIRE(obj3->combat_info.combat_mode == nw::CombatMode::invalid());
    REQUIRE(nwn1::get_equipped_item(obj3, nw::EquipIndex::righthand));
    REQUIRE(nwn1::get_equipped_item(obj3, nw::EquipIndex::righthand)->baseitem == nwn1::base_item_longbow);
    REQUIRE(nwn1::is_ranged_weapon(nwn1::get_equipped_item(obj3, nw::EquipIndex::righthand)));
    REQUIRE(nwn1::get_ability_modifier(obj3, nwn1::ability_strength) == 2);
    REQUIRE(nwn1::get_ability_modifier(obj3, nwn1::ability_dexterity) == 8);
    REQUIRE(30 == nwn1::base_attack_bonus(obj3));
    REQUIRE(47 == nwn1::resolve_attack_bonus(obj3, nwn1::attack_type_onhand));
    // Bane of enemies
    REQUIRE(49 == nwn1::resolve_attack_bonus(obj3, nwn1::attack_type_onhand, vs1));

    auto eff1 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 3);
    eff1->set_versus(vs1->versus_me());
    nw::apply_effect(obj3, eff1);
    auto eff2 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 3);
    eff2->set_versus({nwn1::racial_type_halfling});
    nw::apply_effect(obj3, eff2);
    REQUIRE(52 == nwn1::resolve_attack_bonus(obj3, nwn1::attack_type_onhand, vs1));

    // Dex Weapon Finesse
    auto obj4 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/dexweapfin.utc"));
    REQUIRE(obj4);

    REQUIRE(obj4->stats.has_feat(nwn1::feat_weapon_finesse));
    REQUIRE(obj4->combat_info.combat_mode == nw::CombatMode::invalid());
    REQUIRE(nwn1::get_equipped_item(obj4, nw::EquipIndex::righthand));
    REQUIRE(nwn1::get_equipped_item(obj4, nw::EquipIndex::righthand)->baseitem == nwn1::base_item_dagger);
    REQUIRE(nwn1::get_ability_modifier(obj4, nwn1::ability_strength) == 1);
    REQUIRE(nwn1::get_ability_modifier(obj4, nwn1::ability_dexterity) == 11);
    REQUIRE(25 == nwn1::base_attack_bonus(obj4));
    REQUIRE(40 == nwn1::resolve_attack_bonus(obj4, nwn1::attack_type_onhand));

    // Dual Wield
    auto rh = nwn1::get_equipped_item(obj4, nw::EquipIndex::righthand);
    REQUIRE(nwn1::equip_item(obj4, rh, nw::EquipIndex::lefthand));
    auto [on, off] = nwn1::resolve_number_of_attacks(obj4);
    REQUIRE(on == 4);
    REQUIRE(off == 1);

    REQUIRE(obj4->stats.add_feat(nwn1::feat_improved_two_weapon_fighting));
    auto [on2, off2] = nwn1::resolve_number_of_attacks(obj4);
    REQUIRE(on2 == 4);
    REQUIRE(off2 == 2);

    REQUIRE(38 == nwn1::resolve_attack_bonus(obj4, nwn1::attack_type_onhand));
    REQUIRE(38 == nwn1::resolve_attack_bonus(obj4, nwn1::attack_type_offhand));

    nwk::unload_module();
}

TEST_CASE("creature: attack roll", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);

    auto target = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(target);

    REQUIRE(nwn1::resolve_attack_bonus(obj, nwn1::attack_type_onhand, target) == 45);
    REQUIRE(nwn1::calculate_ac_versus(target, obj, false) == 11);

    for (size_t i = 0; i < 100; ++i) {
        auto result = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, target);
        REQUIRE((result == nw::AttackResult::miss_by_auto_fail || nw::is_attack_type_hit(result)));
    }

    auto eff = nwn1::effect_concealment(100);
    REQUIRE(eff);
    REQUIRE(nw::apply_effect(target, eff));
    for (size_t i = 0; i < 100; ++i) {
        auto result = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, target);
        REQUIRE((result == nw::AttackResult::miss_by_auto_fail || result == nw::AttackResult::miss_by_concealment));
    }
    nw::remove_effect(target, eff);

    auto eff2 = nwn1::effect_miss_chance(100);
    REQUIRE(eff2);
    REQUIRE(nw::apply_effect(obj, eff2));
    for (size_t i = 0; i < 100; ++i) {
        auto result = nwn1::resolve_attack_roll(obj, nwn1::attack_type_onhand, target);
        REQUIRE((result == nw::AttackResult::miss_by_auto_fail || result == nw::AttackResult::miss_by_miss_chance));
    }
}

TEST_CASE("creature: attack type", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);
    obj->combat_info.attacks_onhand = 4;
    REQUIRE(nwn1::resolve_attack_type(obj) == nwn1::attack_type_onhand);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj2);
    obj2->combat_info.attacks_onhand = 1;
    REQUIRE(nwn1::resolve_attack_type(obj2) == nwn1::attack_type_unarmed);
}

TEST_CASE("creature: concealment", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj);
    obj->stats.add_feat(nwn1::feat_epic_self_concealment_30);
    auto [c1, o1] = nwn1::resolve_concealment(obj, obj);
    REQUIRE(c1 == 30);
    REQUIRE_FALSE(o1);

    auto eff = nwn1::effect_concealment(25);
    REQUIRE(eff);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(obj->effects().size() > 0);
    REQUIRE(*obj->effects().begin()->type == *nwn1::effect_type_concealment);
    REQUIRE(nwn1::has_effect_type_applied(obj, nwn1::effect_type_concealment));
    auto [c2, o2] = nwn1::resolve_concealment(obj, obj);
    REQUIRE(c2 == 30);
    REQUIRE_FALSE(o2);

    auto eff2 = nwn1::effect_miss_chance(35);
    REQUIRE(eff2);
    REQUIRE(nw::apply_effect(obj, eff2));
    REQUIRE(obj->effects().size() > 1);
    REQUIRE(nwn1::has_effect_type_applied(obj, nwn1::effect_type_miss_chance));
    auto [c3, o3] = nwn1::resolve_concealment(obj, obj);
    REQUIRE(c3 == 35);
    REQUIRE(o3);
}

TEST_CASE("creature: critical hit multiplier", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);

    int multiplier = nwn1::resolve_critical_multiplier(obj, nwn1::attack_type_onhand);
    REQUIRE(multiplier == 3);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj2);
    int multiplier2 = nwn1::resolve_critical_multiplier(obj2, nwn1::attack_type_onhand);
    REQUIRE(multiplier2 == 2);
}

TEST_CASE("creature: critical hit threat", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);

    int threat = nwn1::resolve_critical_threat(obj, nwn1::attack_type_onhand);
    REQUIRE(threat == 11);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj2);
    int threat2 = nwn1::resolve_critical_threat(obj2, nwn1::attack_type_onhand);
    REQUIRE(threat2 == 1);
    obj2->stats.add_feat(nwn1::feat_improved_critical_unarmed);
    threat2 = nwn1::resolve_critical_threat(obj2, nwn1::attack_type_onhand);
    REQUIRE(threat2 == 2);
}

TEST_CASE("creature: damage - base", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);
    REQUIRE(obj->stats.add_feat(nwn1::feat_weapon_specialization_unarmed));
    auto dice1 = nwn1::resolve_unarmed_damage(obj);
    REQUIRE(dice1.dice == 1);
    REQUIRE(dice1.sides == 20);
    REQUIRE(dice1.bonus == 4);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj2);
    REQUIRE(obj2->stats.add_feat(nwn1::feat_epic_weapon_specialization_scimitar));
    auto weapon2 = nwn1::get_equipped_item(obj2, nw::EquipIndex::righthand);
    REQUIRE(weapon2);
    auto dice2 = nwn1::resolve_weapon_damage(obj2, weapon2->baseitem);
    REQUIRE(dice2.dice == 1);
    REQUIRE(dice2.sides == 6);
    REQUIRE(dice2.bonus == 8);

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj3);
    auto dice3 = nwn1::resolve_unarmed_damage(obj3);
    REQUIRE(dice3.dice == 1);
    REQUIRE(dice3.sides == 2);
}

TEST_CASE("creature: damage - weapon flags", "[objects]")
{
    auto obj1 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj1);
    auto weapon1 = nwn1::get_equipped_item(obj1, nw::EquipIndex::righthand);
    REQUIRE(weapon1);
    REQUIRE(nwn1::resolve_weapon_damage_flags(weapon1).test(nwn1::damage_type_slashing));

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj2);
    auto weapon2 = nwn1::get_equipped_item(obj2, nw::EquipIndex::arms);
    REQUIRE(weapon2);
    REQUIRE(nwn1::resolve_weapon_damage_flags(weapon2).test(nwn1::damage_type_bludgeoning));
}

TEST_CASE("creature: damage immunity", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);

    auto eff = nwn1::effect_damage_immunity(nwn1::damage_type_bludgeoning, 10);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(nwn1::resolve_damage_immunity(obj, nwn1::damage_type_bludgeoning) == 10);

    auto eff2 = nwn1::effect_damage_immunity(nwn1::damage_type_bludgeoning, -3);
    REQUIRE(nw::apply_effect(obj, eff2));
    REQUIRE(nwn1::resolve_damage_immunity(obj, nwn1::damage_type_bludgeoning) == 7);

    // Fake RDD.
    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj2);
    obj2->levels.entries[0].id = nwn1::class_type_dragon_disciple;
    obj2->levels.entries[0].level = 20;
    REQUIRE(nwn1::resolve_damage_immunity(obj2, nwn1::damage_type_fire) == 100);
}

TEST_CASE("creature: damage reduction", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);

    auto eff = nwn1::effect_damage_reduction(30, 2);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(nwn1::resolve_damage_reduction(obj, 1).first == 30);
    REQUIRE(nwn1::resolve_damage_reduction(obj, 3).first == 0);

    auto eff2 = nwn1::effect_damage_reduction(20, 4, 100);
    REQUIRE(nw::apply_effect(obj, eff2));
    auto eff3 = nwn1::effect_damage_reduction(20, 4, 50);
    REQUIRE(nw::apply_effect(obj, eff3));
    REQUIRE(nwn1::resolve_damage_reduction(obj, 1).first == 30);
    REQUIRE(nwn1::resolve_damage_reduction(obj, 3).first == 20);
    REQUIRE(nwn1::resolve_damage_reduction(obj, 4).first == 0);

    // Fake DD.
    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj2);
    obj2->levels.entries[0].id = nwn1::class_type_dwarven_defender;
    obj2->levels.entries[0].level = 20;
    REQUIRE(nwn1::resolve_damage_reduction(obj2, 1).first == 12);

    // Fake Barb.
    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj3);
    obj3->levels.entries[0].id = nwn1::class_type_barbarian;
    obj3->levels.entries[0].level = 20;
    REQUIRE(nwn1::resolve_damage_reduction(obj3, 1).first == 4);

    obj3->stats.add_feat(nwn1::feat_epic_damage_reduction_6);
    REQUIRE(nwn1::resolve_damage_reduction(obj3, 1).first == 10);
}

TEST_CASE("creature: damage resistance", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);

    auto eff = nwn1::effect_damage_resistance(nwn1::damage_type_acid, 10);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(nwn1::resolve_damage_resistance(obj, nwn1::damage_type_acid).first == 10);
    REQUIRE(nwn1::resolve_damage_resistance(obj, nwn1::damage_type_fire).first == 0);

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj3);
    obj3->stats.add_feat(nwn1::feat_epic_energy_resistance_sonic_3);
    REQUIRE(nwn1::resolve_damage_resistance(obj3, nwn1::damage_type_sonic).first == 30);
}

TEST_CASE("creature: hitpoints", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj);

    REQUIRE(nwn1::get_current_hitpoints(obj) == 4);
    REQUIRE(nwn1::get_max_hitpoints(obj) == 4);
    obj->stats.add_feat(nwn1::feat_toughness);
    REQUIRE(nwn1::get_max_hitpoints(obj) == 5);
    obj->stats.add_feat(nwn1::feat_epic_toughness_5);
    REQUIRE(nwn1::get_max_hitpoints(obj) == 105);

    auto eff = nwn1::effect_hitpoints_temporary(10);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(nwn1::get_max_hitpoints(obj) == 115);
    REQUIRE(nw::remove_effect(obj, eff));
    REQUIRE(nwn1::get_max_hitpoints(obj) == 105);
}

TEST_CASE("creature: iteration penalty", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);

    REQUIRE(nwn1::resolve_iteration_penalty(obj, nwn1::attack_type_onhand) == 0);

    obj->combat_info.attack_current = 3;
    REQUIRE(nwn1::resolve_iteration_penalty(obj, nwn1::attack_type_onhand) == 15);

    // Note the guy is hasted
    obj->combat_info.attacks_onhand = 1;
    REQUIRE(nwn1::resolve_iteration_penalty(obj, nwn1::attack_type_offhand) == 5);
}

TEST_CASE("creature: saving throws", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);
    REQUIRE(nwn1::saving_throw(obj, nwn1::saving_throw_fort) == 20);
    REQUIRE(nwn1::saving_throw(obj, nwn1::saving_throw_reflex) == 21);
    REQUIRE(nwn1::saving_throw(obj, nwn1::saving_throw_will) == 13);

    auto eff = nwn1::effect_save_modifier(nwn1::saving_throw_fort, 5);
    REQUIRE(eff);
    REQUIRE(nw::apply_effect(obj, eff));
    REQUIRE(nwn1::saving_throw(obj, nwn1::saving_throw_fort) == 25);

    auto eff2 = nwn1::effect_save_modifier(nwn1::saving_throw_fort, -2);
    REQUIRE(eff2);
    REQUIRE(nw::apply_effect(obj, eff2));
    REQUIRE(nwn1::saving_throw(obj, nwn1::saving_throw_fort) == 23);
}

TEST_CASE("creature: target_state", "[objects]")
{
    nw::TargetState state = nw::TargetState::none;
    state = nwn1::resolve_target_state(nullptr, nullptr);
    REQUIRE(state == nw::TargetState::none);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);
    state = nwn1::resolve_target_state(obj, nullptr);
    REQUIRE(state == nw::TargetState::none);

    auto target = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(target);

    state = nwn1::resolve_target_state(obj, target);
    REQUIRE(to_bool(state & nw::TargetState::flanked));

    target->combat_info.target = obj;
    state = nwn1::resolve_target_state(obj, target);
    REQUIRE_FALSE(to_bool(state & nw::TargetState::flanked));

    target->combat_info.target = nullptr;
    obj->combat_info.target_distance_sq = 200.0f;
    state = nwn1::resolve_target_state(obj, target);
    REQUIRE_FALSE(to_bool(state & nw::TargetState::flanked));
}

TEST_CASE("creature: weapon power", "[objects]")
{
    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);
    REQUIRE(nwn1::resolve_weapon_power(obj, nwn1::get_equipped_item(obj, nw::EquipIndex::righthand)) == 2);

    auto obj2 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(obj2);
    REQUIRE(nwn1::resolve_weapon_power(obj2, nullptr) == 0);

    auto obj3 = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj3);
    REQUIRE(nwn1::resolve_weapon_power(obj3, nwn1::get_equipped_item(obj3, nw::EquipIndex::arms)) == 5);

    obj3->stats.add_feat(nwn1::feat_ki_strike);
    REQUIRE(nwn1::resolve_weapon_power(obj3, nullptr) == 1);
}

TEST_CASE("creature: to_json", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Creature::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/pl_agent_001.utc.json"};
    f << std::setw(4) << j;

    nwk::unload_module();
}

TEST_CASE("creature: json to and from", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Creature::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Creature>();
    REQUIRE(ent2);
    nw::Creature::deserialize(ent2, j, nw::SerializationProfile::blueprint);

    nlohmann::json j2;
    nw::Creature::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);

    nwk::unload_module();
}

TEST_CASE("creature: gff round trip", "[ojbects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    nw::Gff g("test_data/user/development/pl_agent_001.utc");
    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_agent_001_2.utc");

    // Note: the below will typically always fail because the toolset,
    // doesn't sort feats when it writes out the gff.
    // nw::Gff g2("tmp/pl_agent_001_2.utc");
    // REQUIRE(g2.valid());
    // REQUIRE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

    REQUIRE(oa.header.struct_offset == g.head_->struct_offset);
    REQUIRE(oa.header.struct_count == g.head_->struct_count);
    REQUIRE(oa.header.field_offset == g.head_->field_offset);
    REQUIRE(oa.header.field_count == g.head_->field_count);
    REQUIRE(oa.header.label_offset == g.head_->label_offset);
    REQUIRE(oa.header.label_count == g.head_->label_count);
    REQUIRE(oa.header.field_data_offset == g.head_->field_data_offset);
    REQUIRE(oa.header.field_data_count == g.head_->field_data_count);
    REQUIRE(oa.header.field_idx_offset == g.head_->field_idx_offset);
    REQUIRE(oa.header.field_idx_count == g.head_->field_idx_count);
    REQUIRE(oa.header.list_idx_offset == g.head_->list_idx_offset);
    REQUIRE(oa.header.list_idx_count == g.head_->list_idx_count);

    nwk::unload_module();
}

TEST_CASE("creature: equip and unequip items", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/test_creature.utc"));
    REQUIRE(obj);
    REQUIRE(obj->instantiate());

    auto item = nwk::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(item);

    REQUIRE(nwn1::equip_item(obj, item, nw::EquipIndex::chest));
    REQUIRE(nwn1::get_equipped_item(obj, nw::EquipIndex::chest));

    auto item1 = nwn1::unequip_item(obj, nw::EquipIndex::chest);
    REQUIRE(item1);
    REQUIRE_FALSE(nwn1::get_equipped_item(obj, nw::EquipIndex::chest));

    REQUIRE_FALSE(nwn1::equip_item(obj, item, nw::EquipIndex::head));

    auto item2 = nwn1::unequip_item(obj, nw::EquipIndex::head);
    REQUIRE_FALSE(item2);

    auto boots_of_speed = nwk::objects().load<nw::Item>("nw_it_mboots005"sv);
    REQUIRE(boots_of_speed);
    REQUIRE(nwn1::equip_item(obj, boots_of_speed, nw::EquipIndex::boots));
    REQUIRE(obj->hasted);
    auto boots_of_speed2 = nwn1::unequip_item(obj, nw::EquipIndex::boots);
    REQUIRE(boots_of_speed2);
    REQUIRE_FALSE(obj->hasted);

    nwk::unload_module();
}

TEST_CASE("creature: apply and remove effects", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/test_creature.utc"));
    REQUIRE(obj);
    REQUIRE(obj->instantiate());

    auto eff = nwn1::effect_haste();
    REQUIRE(obj->effects().add(eff));
    REQUIRE(nw::has_effect_applied(obj, nwn1::effect_type_haste));
    REQUIRE(obj->effects().size());
    REQUIRE(obj->effects().remove(eff));
    REQUIRE_FALSE(nw::has_effect_applied(obj, nwn1::effect_type_haste));
    REQUIRE(obj->effects().size() == 0);
    REQUIRE_FALSE(obj->effects().remove(nullptr));

    nwk::unload_module();
}
