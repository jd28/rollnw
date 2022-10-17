#include <catch2/catch_all.hpp>

#include <nw/components/Sound.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("sound: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Sound>(fs::path("test_data/user/development/blue_bell.uts"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "blue_bell");
}

TEST_CASE("sound: json round trip", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Sound>(fs::path("test_data/user/development/blue_bell.uts"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Sound::serialize(ent, j, nw::SerializationProfile::blueprint);

    nw::Sound ent2;
    REQUIRE(nw::Sound::deserialize(&ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::Sound::serialize(&ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);

    std::ofstream f{"tmp/blue_bell.uts.json"};
    f << std::setw(4) << j;
}

TEST_CASE("sound: gff round trip", "[ojbects]")
{
    nw::Gff g("test_data/user/development/blue_bell.uts");
    REQUIRE(g.valid());

    nw::Sound ent;
    nw::Sound::deserialize(&ent, g.toplevel(), nw::SerializationProfile::blueprint);

    nw::GffBuilder oa = nw::Sound::serialize(&ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/blue_bell.uts");

    nw::Gff g2("tmp/blue_bell.uts");
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
