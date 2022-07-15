#include <catch2/catch.hpp>

#include <nw/components/Creature.hpp>
#include <nw/kernel/Kernel.hpp>
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
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/nw_chicken.utc"));
    REQUIRE(ent.is_alive());

    auto cre = ent.get<nw::Creature>();
    auto common = ent.get<nw::Common>();
    auto appear = ent.get<nw::Appearance>();
    auto stats = ent.get<nw::CreatureStats>();
    auto scripts = ent.get<nw::CreatureScripts>();

    REQUIRE(common->resref == "nw_chicken");
    REQUIRE(stats->abilities[2] == 8);
    REQUIRE(scripts->on_attacked == "nw_c2_default5");
    REQUIRE(appear->id == 31);
    REQUIRE(cre->gender == 1);
}

TEST_CASE("creature: load pl_agent_001", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto cre = ent.get<nw::Creature>();
    auto common = ent.get<nw::Common>();
    auto appearance = ent.get<nw::Appearance>();
    auto stats = ent.get<nw::CreatureStats>();
    auto scripts = ent.get<nw::CreatureScripts>();
    auto equipment = ent.get<nw::Equips>();
    auto combat_info = ent.get<nw::CombatInfo>();

    REQUIRE(cre);
    REQUIRE(common);
    REQUIRE(appearance);
    REQUIRE(stats);
    REQUIRE(scripts);
    REQUIRE(equipment);
    REQUIRE(combat_info);

    REQUIRE(common->resref == "pl_agent_001");
    REQUIRE(stats->abilities[2] == 16);
    REQUIRE(scripts->on_attacked == "mon_ai_5attacked");
    REQUIRE(appearance->id == 6);
    REQUIRE(appearance->body_parts.shin_left == 1);
    REQUIRE(cre->soundset == 171);
    REQUIRE(std::get<nw::Resref>(equipment->equips[1]) == "dk_agent_thread2");
    REQUIRE(combat_info->ac_natural == 0);
    REQUIRE(combat_info->special_abilities.size() == 1);
    REQUIRE(combat_info->special_abilities[0].spell == 120);
}

TEST_CASE("creature: add/has/remove_feat", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    auto stats = ent.get_mut<nw::CreatureStats>();
    REQUIRE(stats->feats().size() == 37);
    REQUIRE(stats->has_feat(stats->feats()[20]));
    REQUIRE_FALSE(stats->add_feat(stats->feats()[20]));
}

TEST_CASE("creature: feat search", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nwk::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

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

// TEST_CASE("creature: base attack bonus", "[objects]")
// {
//     auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
//     REQUIRE(mod);

//     auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
//     REQUIRE(ent.is_alive());

//     REQUIRE(32 == nwn1::attack_bonus(ent, true));

//     nwk::unload_module();
// }

TEST_CASE("creature: skills ", "[objects]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    REQUIRE(nwn1::get_skill_rank(ent, nwn1::skill_discipline, true) == 40);
    ent.get_mut<nw::CreatureStats>()->add_feat(nwn1::feat_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(ent, nwn1::skill_discipline, false) == 58);
    ent.get_mut<nw::CreatureStats>()->add_feat(nwn1::feat_epic_skill_focus_discipline);
    REQUIRE(nwn1::get_skill_rank(ent, nwn1::skill_discipline, false) == 68);

    nwk::unload_module();
}

TEST_CASE("creature: to_json", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/pl_agent_001.utc.json"};
    f << std::setw(4) << j;
}

TEST_CASE("creature: json to and from", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().deserialize(
        nw::ObjectType::creature,
        j,
        nw::SerializationProfile::blueprint);

    nlohmann::json j2;
    nw::kernel::objects().serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);
}

TEST_CASE("creature: gff round trip", "[ojbects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_agent_001.utc"));
    REQUIRE(ent.is_alive());

    REQUIRE(ent.is_alive());

    nw::GffInputArchive g("test_data/user/development/pl_agent_001.utc");
    nw::GffOutputArchive oa = nw::kernel::objects().serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_agent_001_2.utc");

    // Note: the below will typically always fail because the toolset,
    // doesn't sort feats when it writes out the gff.
    // nw::GffInputArchive g2("tmp/pl_agent_001_2.utc");
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
