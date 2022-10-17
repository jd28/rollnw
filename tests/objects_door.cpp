#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>
#include <nw/components/Door.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/serialization/Archives.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("door: from_gff", "[objects]")
{
    auto door = nw::kernel::objects().load<nw::Door>(fs::path("test_data/user/development/door_ttr_002.utd"));

    REQUIRE(door->common.resref == "door_ttr_002");
    REQUIRE(door->appearance == 0);
    REQUIRE(!door->plot);
    REQUIRE(!door->lock.locked);
}

TEST_CASE("door: to_json", "[objects]")
{
    auto door = nw::kernel::objects().load<nw::Door>(fs::path("test_data/user/development/door_ttr_002.utd"));

    nlohmann::json j;
    REQUIRE(nw::Door::serialize(door, j, nw::SerializationProfile::blueprint));

    nw::Door door2;
    REQUIRE(nw::Door::deserialize(&door2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    REQUIRE(nw::Door::serialize(&door2, j2, nw::SerializationProfile::blueprint));
    REQUIRE(j == j2);

    std::ofstream f{"tmp/door_ttr_002.utd.json"};
    f << std::setw(4) << j;
}

TEST_CASE("door: gff round trip", "[ojbects]")
{
    nw::Gff g("test_data/user/development/door_ttr_002.utd");
    REQUIRE(g.valid());

    auto door = nw::kernel::objects().load<nw::Door>(fs::path("test_data/user/development/door_ttr_002.utd"));

    nw::GffBuilder oa = nw::Door::serialize(door, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/door_ttr_002.utd");

    nw::Gff g2("tmp/door_ttr_002.utd");
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
