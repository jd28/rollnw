#include <catch2/catch.hpp>

#include <nw/objects/Waypoint.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("waypoint: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/wp_behexit001.utw"};
    REQUIRE(g.valid());
    REQUIRE(g.valid());
    nw::Waypoint w{g.toplevel(), nw::SerializationProfile::blueprint};

    REQUIRE(w.common()->resref == "wp_behexit001");
    REQUIRE(w.linked_to == "");
}

TEST_CASE("waypoint: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/wp_behexit001.utw"};
    REQUIRE(g.valid());
    nw::Waypoint w{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = w.to_json(nw::SerializationProfile::blueprint);

    nw::Waypoint w2;
    w2.from_json(j, nw::SerializationProfile::blueprint);

    REQUIRE(w2.common()->resref == "wp_behexit001");
    REQUIRE(w2.linked_to == "");

    std::ofstream f{"tmp/wp_behexit001.utw.json"};
    f << std::setw(4) << j;
}

TEST_CASE("waypoint: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/wp_behexit001.utw"};
    REQUIRE(g.valid());
    nw::Waypoint w{g.toplevel(), nw::SerializationProfile::blueprint};
    nlohmann::json j = w.to_json(nw::SerializationProfile::blueprint);
    nw::Waypoint w2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = w2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}

TEST_CASE("waypoint: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/wp_behexit001.utw");
    REQUIRE(g.valid());

    nw::Waypoint way{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa = way.to_gff(nw::SerializationProfile::blueprint);

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
