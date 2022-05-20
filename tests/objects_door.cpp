#include <catch2/catch.hpp>

#include <nlohmann/json.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Door.hpp>
#include <nw/serialization/Archives.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("door: from_gff", "[objects]")
{
    auto door = nw::kernel::objects().load(fs::path("test_data/user/development/door_ttr_002.utd"));

    auto d = door.get<nw::Door>();
    auto common = door.get<nw::Common>();
    auto lock = door.get<nw::Lock>();

    REQUIRE(common->resref == "door_ttr_002");
    REQUIRE(d->appearance == 0);
    REQUIRE(!d->plot);
    REQUIRE(!lock->locked);

    door.destruct();
}

TEST_CASE("door: to_json", "[objects]")
{
    auto door = nw::kernel::objects().load(fs::path("test_data/user/development/door_ttr_002.utd"));

    nlohmann::json j;
    nw::kernel::objects().serialize(door, j, nw::SerializationProfile::blueprint);

    auto door2 = nw::kernel::objects().deserialize(nw::ObjectType::door, j,
        nw::SerializationProfile::blueprint);
    REQUIRE(door2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(door, j2, nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);

    std::ofstream f{"tmp/door_ttr_002.utd.json"};
    f << std::setw(4) << j;
}

TEST_CASE("door: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/door_ttr_002.utd");
    REQUIRE(g.valid());

    auto door = nw::kernel::objects().load(fs::path("test_data/user/development/door_ttr_002.utd"));

    nw::GffOutputArchive oa = nw::kernel::objects().serialize(door, nw::SerializationProfile::blueprint);
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
