#include <catch2/catch_all.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/Archives.hpp>
#include <nwn1/Profile.hpp>
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
    REQUIRE(ent->combat_info.ac_natural == 0);
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

    REQUIRE(nwn1::knows_feat(ent, nwn1::feat_epic_toughness_2));
    REQUIRE_FALSE(nwn1::knows_feat(ent, nwn1::feat_epic_toughness_1));

    auto [feat, nth] = nwn1::has_feat_successor(ent, nwn1::feat_epic_toughness_2);
    REQUIRE(nth);
    REQUIRE(feat == nwn1::feat_epic_toughness_4);

    auto [feat2, nth2] = nwn1::has_feat_successor(ent, nwn1::feat_epic_great_wisdom_1);
    REQUIRE(nth2 == 0);

    auto feat3 = nwn1::highest_feat_in_range(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    REQUIRE(feat3 == nwn1::feat_epic_toughness_4);

    auto n = nwn1::count_feats_in_range(ent, nwn1::feat_epic_toughness_1, nwn1::feat_epic_toughness_10);
    REQUIRE(n == 3); // char doesn't have et1.

    nwk::unload_module();
}

TEST_CASE("creature: base attack bonus", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);

    REQUIRE(27 == nwn1::base_attack_bonus(obj));
    REQUIRE(42 == nwn1::attack_bonus(obj, nwn1::attack_type_unarmed));

    REQUIRE_FALSE(obj->stats.has_feat(nwn1::feat_weapon_focus_unarmed));
    REQUIRE_FALSE(obj->stats.has_feat(nwn1::feat_epic_weapon_focus_unarmed));
    obj->stats.add_feat(nwn1::feat_weapon_focus_unarmed);
    obj->stats.add_feat(nwn1::feat_epic_weapon_focus_unarmed);

    REQUIRE(45 == nwn1::attack_bonus(obj, nwn1::attack_type_unarmed));
    auto eff1 = nwn1::effect_attack_modifier(nwn1::attack_type_any, 2);
    REQUIRE(nwn1::apply_effect(obj, eff1));
    REQUIRE(47 == nwn1::attack_bonus(obj, nwn1::attack_type_unarmed));
    auto eff2 = nwn1::effect_attack_modifier(nwn1::attack_type_unarmed, 3);
    REQUIRE(nwn1::apply_effect(obj, eff2));
    REQUIRE(50 == nwn1::attack_bonus(obj, nwn1::attack_type_unarmed));

    nwk::unload_module();
}

TEST_CASE("creature: attack bonus", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(obj);
    REQUIRE(obj->instantiate());

    auto vs = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/drorry.utc"));
    REQUIRE(vs);
    REQUIRE(vs->instantiate());

    REQUIRE(obj->levels.level() == 38);
    REQUIRE(obj->levels.level_by_class(nwn1::class_type_fighter) == 10);
    REQUIRE(obj->levels.level_by_class(nwn1::class_type_weapon_master) == 28);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength) == 16);
    REQUIRE(nwn1::get_ability_modifier(obj, nwn1::ability_strength) == 3);
    REQUIRE(obj->size_ab_modifier == 1);
    REQUIRE(29 == nwn1::base_attack_bonus(obj));
    REQUIRE(43 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_mode = nwn1::combat_mode_power_attack;
    REQUIRE(38 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_mode = nwn1::combat_mode_improved_power_attack;
    REQUIRE(33 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand));

    obj->combat_mode = nwn1::combat_mode_expertise;
    REQUIRE(38 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_mode = nwn1::combat_mode_improved_expertise;
    REQUIRE(33 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand));
    obj->combat_mode = nw::CombatMode::invalid();
    REQUIRE(43 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand));

    obj->stats.add_feat(nwn1::feat_epic_prowess);
    REQUIRE(44 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand));

    obj->target_state = nw::TargetState::flanked;
    REQUIRE(46 == nwn1::attack_bonus(obj, nwn1::attack_type_onhand, vs));

    nwk::unload_module();
}

TEST_CASE("creature: skills ", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto obj = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(obj);

    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline, true) == 40);
    obj->stats.add_feat(nwn1::feat_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline, false) == 61);
    obj->stats.add_feat(nwn1::feat_epic_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline, false) == 71);

    auto eff = nwn1::effect_skill_modifier(nwn1::skill_discipline, 5);
    REQUIRE(nwn1::apply_effect(obj, eff));
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline, false) == 76);

    auto eff2 = nwn1::effect_ability_modifier(nwn1::ability_strength, 5);
    REQUIRE(nwn1::apply_effect(obj, eff2));
    REQUIRE(nwn1::get_skill_rank(obj, nwn1::skill_discipline, false) == 78);

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
    REQUIRE(nwn1::apply_effect(obj, eff));
    REQUIRE(obj->effects().size() > 0);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 47);

    // Belt of Cloud Giant Strength
    auto item = nwk::objects().load<nw::Item>("x2_it_mbelt001"sv);
    REQUIRE(item);
    REQUIRE(nwn1::equip_item(obj, item, nw::EquipIndex::belt));
    REQUIRE(obj->effects().size() > 1);
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 54);
    REQUIRE(nwn1::unequip_item(obj, nw::EquipIndex::belt));
    REQUIRE(nwn1::get_ability_score(obj, nwn1::ability_strength, false) == 47);

    nwk::unload_module();
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
    nw::GffBuilder oa = nw::Creature::serialize(ent, nw::SerializationProfile::blueprint);
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
    REQUIRE(nwn1::has_effect_applied(obj, nwn1::effect_type_haste));
    REQUIRE(obj->effects().size());
    REQUIRE(obj->effects().remove(eff));
    REQUIRE_FALSE(nwn1::has_effect_applied(obj, nwn1::effect_type_haste));
    REQUIRE(obj->effects().size() == 0);
    REQUIRE_FALSE(obj->effects().remove(nullptr));

    nwk::unload_module();
}
