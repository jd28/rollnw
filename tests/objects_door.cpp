#include <catch2/catch.hpp>

#include <nw/objects/Door.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("door: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/door_ttr_002.utd"};
    REQUIRE(g.valid());
    nw::Door d{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(d.valid());

    REQUIRE(d.common()->resref == "door_ttr_002");
    REQUIRE(d.appearance == 0);
    REQUIRE(!d.plot);
    REQUIRE(!d.lock.locked);
}

TEST_CASE("door: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/development/door_ttr_002.utd"};
    REQUIRE(g.valid());
    nw::Door d{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(d.valid());

    nlohmann::json j = d.to_json(nw::SerializationProfile::blueprint);
    nw::Door d2{j, nw::SerializationProfile::blueprint};
    REQUIRE(d2.valid());

    nlohmann::json j2 = d2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);

    nw::Lock l;
    l.from_json(d.lock.to_json());
    REQUIRE(l.lockable == d.lock.lockable);

    std::ofstream f{"tmp/door_ttr_002.utd.json"};
    f << std::setw(4) << j;
}

TEST_CASE("door: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/door_ttr_002.utd");
    REQUIRE(g.valid());

    nw::Door door{g.toplevel(), nw::SerializationProfile::blueprint};
    nw::GffOutputArchive oa = door.to_gff(nw::SerializationProfile::blueprint);
    oa.write_to("tmp/door_ttr_002.utd");

    nw::GffInputArchive g2("tmp/door_ttr_002.utd");
    REQUIRE(g2.valid());
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
