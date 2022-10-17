#include <catch2/catch_all.hpp>

#include <nw/components/Encounter.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("encounter: from_gff", "[objects]")
{
    auto enc = nw::kernel::objects().load<nw::Encounter>(fs::path("test_data/user/development/boundelementallo.ute"));
    REQUIRE(enc);

    REQUIRE(enc->common.resref == "boundelementallo");
    REQUIRE(!!enc->player_only);
    REQUIRE(enc->scripts.on_entered == "");
    REQUIRE(enc->creatures.size() > 0);
    REQUIRE(enc->creatures[0].resref == "tyn_bound_acid_l");
}

TEST_CASE("encounter: to_json", "[objects]")
{
    auto enc = nw::kernel::objects().load<nw::Encounter>(fs::path("test_data/user/development/boundelementallo.ute"));
    REQUIRE(enc);

    nlohmann::json j;
    nw::Encounter::serialize(enc, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/boundelementallo.ute.json"};
    f << std::setw(4) << j;
}

TEST_CASE("encounter: json back and forth", "[objects]")
{
    auto enc = nw::kernel::objects().load<nw::Encounter>(fs::path("test_data/user/development/boundelementallo.ute"));
    REQUIRE(enc);

    nlohmann::json j;
    nw::Encounter::serialize(enc, j, nw::SerializationProfile::blueprint);

    nw::Encounter enc2;
    REQUIRE(nw::Encounter::deserialize(&enc2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::Encounter::serialize(enc, j2, nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}

TEST_CASE("encount: gff round trip", "[ojbects]")
{
    nw::Gff g("test_data/user/development/boundelementallo.ute");
    REQUIRE(g.valid());

    nw::Encounter enc;
    REQUIRE(nw::Encounter::deserialize(&enc, g.toplevel(),
        nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = nw::Encounter::serialize(&enc, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/boundelementallo_2.ute");
    nw::Gff g2{"tmp/boundelementallo_2.ute"};

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
