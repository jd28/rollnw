#include <catch2/catch_all.hpp>

#include <nw/components/Waypoint.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("waypoint: from_gff", "[objects]")
{
    auto obj = nw::kernel::objects().load<nw::Waypoint>(fs::path("test_data/user/development/wp_behexit001.utw"));
    REQUIRE(obj);
}

TEST_CASE("waypoint: json round trip", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Waypoint>(fs::path("test_data/user/development/wp_behexit001.utw"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Waypoint::serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().make<nw::Waypoint>();
    REQUIRE(ent2);
    REQUIRE(nw::Waypoint::deserialize(ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::Waypoint::serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);

    std::ofstream f{"tmp/wp_behexit001.utw.json"};
    f << std::setw(4) << j;
}

TEST_CASE("waypoint: gff round trip", "[ojbects]")
{
    nw::Gff g("test_data/user/development/wp_behexit001.utw");
    REQUIRE(g.valid());

    auto ent = nw::kernel::objects().make<nw::Waypoint>();
    REQUIRE(ent);

    REQUIRE(nw::Waypoint::deserialize(ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = nw::Waypoint::serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/wp_behexit001.utw");

    nw::Gff g2{"tmp/wp_behexit001.utw"};
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

    REQUIRE(g2.head_->struct_offset == g.head_->struct_offset);
    REQUIRE(g2.head_->struct_count == g.head_->struct_count);
    REQUIRE(g2.head_->field_offset == g.head_->field_offset);
    REQUIRE(g2.head_->field_count == g.head_->field_count);
    REQUIRE(g2.head_->label_offset == g.head_->label_offset);
    REQUIRE(g2.head_->label_count == g.head_->label_count);
    REQUIRE(g2.head_->field_data_offset == g.head_->field_data_offset);
    REQUIRE(g2.head_->field_data_count == g.head_->field_data_count);
    REQUIRE(g2.head_->field_idx_offset == g.head_->field_idx_offset);
    REQUIRE(g2.head_->field_idx_count == g.head_->field_idx_count);
    REQUIRE(g2.head_->list_idx_offset == g.head_->list_idx_offset);
    REQUIRE(g2.head_->list_idx_count == g.head_->list_idx_count);
}
