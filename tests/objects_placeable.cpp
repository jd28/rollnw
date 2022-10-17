#include <catch2/catch_all.hpp>

#include <nw/components/Placeable.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/serialization/Archives.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("placeable: from_gff", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Placeable>(fs::path("test_data/user/development/arrowcorpse001.utp"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "arrowcorpse001");
    REQUIRE(ent->appearance == 109);
    REQUIRE(!ent->plot);
    REQUIRE(ent->static_);
    REQUIRE(!ent->useable);
}

TEST_CASE("placeable: json roundtrip", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Placeable>(fs::path("test_data/user/development/arrowcorpse001.utp"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Placeable::serialize(ent, j, nw::SerializationProfile::blueprint);

    nw::Placeable ent2;
    REQUIRE(nw::Placeable::deserialize(&ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::Placeable::serialize(&ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);

    std::ofstream f{"tmp/arrowcorpse001.utp.json"};
    f << std::setw(4) << j;
}

TEST_CASE("placeable: gff round trip", "[ojbects]")
{
    nw::Gff g("test_data/user/development/arrowcorpse001.utp");
    REQUIRE(g.valid());

    nw::Placeable ent;
    REQUIRE(nw::Placeable::deserialize(&ent, g.toplevel(), nw::SerializationProfile::blueprint));

    nw::GffBuilder oa = nw::Placeable::serialize(&ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/arrowcorpse001.utp");

    nw::Gff g2{"tmp/arrowcorpse001.utp"};
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
