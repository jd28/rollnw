#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("encounter: from_gff", "[objects]")
{
    auto enc = nw::kernel::objects().load(fs::path("test_data/user/development/boundelementallo.ute"));
    REQUIRE(enc.is_alive());

    auto e = enc.get<nw::Encounter>();
    auto common = enc.get<nw::Common>();
    auto scripts = enc.get<nw::EncounterScripts>();

    REQUIRE(e);
    REQUIRE(common);
    REQUIRE(scripts);

    REQUIRE(common->resref == "boundelementallo");
    REQUIRE(!!e->player_only);
    REQUIRE(scripts->on_entered == "");
    REQUIRE(e->creatures.size() > 0);
    REQUIRE(e->creatures[0].resref == "tyn_bound_acid_l");
}

TEST_CASE("encounter: to_json", "[objects]")
{
    auto enc = nw::kernel::objects().load(fs::path("test_data/user/development/boundelementallo.ute"));
    REQUIRE(enc.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(enc, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/boundelementallo.ute.json"};
    f << std::setw(4) << j;
}

TEST_CASE("encounter: json back and forth", "[objects]")
{
    auto enc = nw::kernel::objects().load(fs::path("test_data/user/development/boundelementallo.ute"));
    REQUIRE(enc.is_alive());

    REQUIRE(enc.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(enc, j, nw::SerializationProfile::blueprint);

    auto enc2 = nw::kernel::objects().deserialize(nw::ObjectType::encounter,
        j,
        nw::SerializationProfile::blueprint);
    REQUIRE(enc2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(enc, j2, nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}

TEST_CASE("encount: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/boundelementallo.ute");
    REQUIRE(g.valid());

    auto enc = nw::kernel::objects().deserialize(nw::ObjectType::encounter,
        g.toplevel(),
        nw::SerializationProfile::blueprint);
    REQUIRE(enc.is_alive());

    nw::GffOutputArchive oa = nw::kernel::objects().serialize(enc, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/boundelementallo_2.ute");
    nw::GffInputArchive g2{"tmp/boundelementallo_2.ute"};

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
