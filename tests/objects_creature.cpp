#include <catch2/catch.hpp>

#include <nw/objects/Creature.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nw/serialization/conversions.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("Loading nw_chicken", "[objects]")
{
    nw::GffInputArchive g{"test_data/nw_chicken.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(c.common()->resref == "nw_chicken");
    REQUIRE(c.stats.abilities[2] == 8);
    REQUIRE(c.scripts.on_attacked == "nw_c2_default5");
    REQUIRE(c.appearance.id == 31);
    REQUIRE(c.gender == 1);
}

TEST_CASE("Loading test_creature", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(c.common()->resref == "pl_agent_001");
    REQUIRE(c.stats.abilities[2] == 16);
    REQUIRE(c.scripts.on_attacked == "mon_ai_5attacked");
    REQUIRE(c.appearance.id == 6);
    REQUIRE(c.appearance.body_parts.left_shin == 1);
    REQUIRE(c.stats.feats.size() == 37);
    REQUIRE(c.soundset == 171);
    REQUIRE(std::get<nw::Resref>(c.equipment.equips[1]) == "dk_agent_thread2");
    REQUIRE(c.combat_info.ac_natural == 0);
    REQUIRE(c.combat_info.special_abilities.size() == 1);
    REQUIRE(c.combat_info.special_abilities[0].spell == 120);
}

TEST_CASE("creature: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = c.to_json(nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/pl_agent_001.utc.json"};
    f << std::setw(4) << j;
}

TEST_CASE("creature: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};
    nlohmann::json j = c.to_json(nw::SerializationProfile::blueprint);
    nw::Creature c2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = c2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}

TEST_CASE("creature: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/pl_agent_001.utc");
    REQUIRE(g.valid());

    nw::Creature cre{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa{"UTC"};
    cre.to_gff(oa.top, nw::SerializationProfile::blueprint);
    oa.build();
    oa.write_to("tmp/pl_agent_001_2.utc");

    // Note: the below will typically always fail because the toolset,
    // doesn't sort feats when it writes out the gff.
    // nw::GffInputArchive g2("tmp/pl_agent_001_2.utc");
    // REQUIRE(g2.valid());
    // REQUIRE(nw::gff_to_json(g) == nw::gff_to_json(g2));

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
