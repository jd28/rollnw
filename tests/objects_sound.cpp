#include <catch2/catch.hpp>

#include <nw/objects/Sound.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("sound: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/blue_bell.uts"};
    REQUIRE(g.valid());
    nw::Sound s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.common()->resref == "blue_bell");
}

TEST_CASE("sound: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/blue_bell.uts"};
    REQUIRE(g.valid());
    nw::Sound s{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = s.to_json(nw::SerializationProfile::blueprint);
    nw::Sound s2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = s2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(s2.common()->resref == "blue_bell");
    REQUIRE(j == j2);

    std::ofstream f{"tmp/blue_bell.uts.json"};
    f << std::setw(4) << j;
}

TEST_CASE("sound: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/blue_bell.uts");
    REQUIRE(g.valid());

    nw::Sound sound{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa = sound.to_gff(nw::SerializationProfile::blueprint);

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
