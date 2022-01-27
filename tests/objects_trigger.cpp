#include <catch2/catch.hpp>

#include <nw/objects/Trigger.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("trigger: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_spray_sewage.utt"};
    REQUIRE(g.valid());
    nw::Trigger t{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(t.common()->resref == "pl_spray_sewage");
    REQUIRE(t.portrait == 0);
    REQUIRE(t.loadscreen == 0);
    REQUIRE(t.scripts.on_enter == "pl_trig_sewage");
}

TEST_CASE("trigger: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_spray_sewage.utt"};
    REQUIRE(g.valid());
    nw::Trigger t{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = t.to_json(nw::SerializationProfile::blueprint);
    nw::Trigger t2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = t2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);

    std::ofstream f{"tmp/pl_spray_sewage.utt.json"};
    f << std::setw(4) << j;
}

TEST_CASE("trigger: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/pl_spray_sewage.utt");
    REQUIRE(g.valid());

    nw::Trigger trig{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa = trig.to_gff(nw::SerializationProfile::blueprint);
    oa.write_to("tmp/pl_spray_sewage_2.utt");
    nw::GffInputArchive g2{"tmp/pl_spray_sewage_2.utt"};
    REQUIRE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

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
