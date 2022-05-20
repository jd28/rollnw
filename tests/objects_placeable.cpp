#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("placeable: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/arrowcorpse001.utp"));
    REQUIRE(ent.is_alive());

    auto p = ent.get<nw::Placeable>();
    auto common = ent.get<nw::Common>();
    // auto scripts = ent.get<nw::PlaceableScripts>();

    REQUIRE(p);
    REQUIRE(common);

    REQUIRE(common->resref == "arrowcorpse001");
    REQUIRE(p->appearance == 109);
    REQUIRE(!p->plot);
    REQUIRE(p->static_);
    REQUIRE(!p->useable);
}

TEST_CASE("placeable: json roundtrip", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/arrowcorpse001.utp"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().deserialize(nw::ObjectType::placeable,
        j,
        nw::SerializationProfile::blueprint);
    REQUIRE(ent2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);

    std::ofstream f{"tmp/arrowcorpse001.utp.json"};
    f << std::setw(4) << j;
}

TEST_CASE("placeable: gff round trip", "[ojbects]")
{
    nw::GffInputArchive g("test_data/user/development/arrowcorpse001.utp");
    REQUIRE(g.valid());

    auto ent = nw::kernel::objects().deserialize(nw::ObjectType::placeable,
        g.toplevel(),
        nw::SerializationProfile::blueprint);
    REQUIRE(ent.is_alive());

    nw::GffOutputArchive oa = nw::kernel::objects().serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/arrowcorpse001.utp");

    nw::GffInputArchive g2{"tmp/arrowcorpse001.utp"};
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
