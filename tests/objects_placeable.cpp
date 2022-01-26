#include <catch2/catch.hpp>

#include <nw/objects/Placeable.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("placeable: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/arrowcorpse001.utp"};
    REQUIRE(g.valid());
    nw::Placeable p{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(p.common()->resref == "arrowcorpse001");
    REQUIRE(p.appearance == 109);
    REQUIRE(!p.plot);
    REQUIRE(p.static_);
    REQUIRE(!p.useable);
}

TEST_CASE("placeable: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/arrowcorpse001.utp"};
    REQUIRE(g.valid());
    nw::Placeable p{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = p.to_json(nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/arrowcorpse001.utp.json"};
    f << std::setw(4) << j;
}

TEST_CASE("placeable: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/arrowcorpse001.utp"};
    REQUIRE(g.valid());
    nw::Placeable p{g.toplevel(), nw::SerializationProfile::blueprint};
    nlohmann::json j = p.to_json(nw::SerializationProfile::blueprint);
    nw::Placeable p2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = p2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}

TEST_CASE("placeable: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/arrowcorpse001.utp");
    REQUIRE(g.valid());

    nw::Placeable place{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa{"UTP"};
    place.to_gff(oa.top, nw::SerializationProfile::blueprint);
    oa.build();

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
