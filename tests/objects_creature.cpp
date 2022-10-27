#include <catch2/catch_all.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/rules/Feat.hpp>
#include <nw/serialization/Archives.hpp>
#include <nwn1/Profile.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST_CASE("creature: load nw_chicken", "[objects]")
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(ent);
    REQUIRE(nwk::objects().valid(ent->handle()));

    REQUIRE(ent->common.resref == "nw_chicken");
    REQUIRE(ent->stats.get_ability_score(nwn1::ability_dexterity) == 7);
    REQUIRE(ent->scripts.on_attacked == "nw_c2_default5");
    REQUIRE(ent->appearance.id == 31);
    REQUIRE(ent->gender == 1);
}

TEST_CASE("creature: load pl_agent_001", "[objects]")
{
    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "pl_agent_001");
    REQUIRE(ent->stats.get_ability_score(nwn1::ability_dexterity) == 13);
    REQUIRE(ent->scripts.on_attacked == "mon_ai_5attacked");
    REQUIRE(ent->appearance.id == 6);
    REQUIRE(ent->appearance.body_parts.shin_left == 1);
    REQUIRE(ent->soundset == 171);
    REQUIRE(std::get<nw::Resref>(ent->equipment.equips[1]) == "dk_agent_thread2");
    REQUIRE(ent->combat_info.ac_natural == 0);
    REQUIRE(ent->combat_info.special_abilities.size() == 1);
    REQUIRE(ent->combat_info.special_abilities[0].spell == 120);
}

TEST_CASE("creature: add/has/remove_feat", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(ent->stats.feats().size() == 37);
    REQUIRE(ent->stats.has_feat(ent->stats.feats()[20]));
    REQUIRE_FALSE(ent->stats.add_feat(ent->stats.feats()[20]));
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

    auto ent = nwk::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(32 == nwn1::attack_bonus(ent, true));

    nwk::unload_module();
}

TEST_CASE("creature: skills ", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(nwn1::get_skill_rank(ent, nwn1::skill_discipline, true) == 40);
    ent->stats.add_feat(nwn1::feat_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(ent, nwn1::skill_discipline, false) == 58);
    ent->stats.add_feat(nwn1::feat_epic_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(ent, nwn1::skill_discipline, false) == 68);

    nwk::unload_module();
}

TEST_CASE("creature: ability ", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    REQUIRE(nwn1::get_ability_score(ent, nwn1::ability_strength, false) == 40);
    ent->stats.add_feat(nwn1::feat_epic_great_strength_1);
    REQUIRE(nwn1::get_ability_score(ent, nwn1::ability_strength, false) == 41);
    ent->stats.add_feat(nwn1::feat_epic_great_strength_2);
    REQUIRE(nwn1::get_ability_score(ent, nwn1::ability_strength, false) == 42);

    nwk::unload_module();
}

TEST_CASE("creature: to_json", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Creature>(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Creature::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/pl_agent_001.utc.json"};
    f << std::setw(4) << j;
}

TEST_CASE("creature: json to and from", "[objects]")
{
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
}

TEST_CASE("creature: gff round trip", "[ojbects]")
{
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
}
