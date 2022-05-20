#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Item.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("item: load armor item", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent.is_alive());

    auto item = ent.get<nw::Item>();
    auto common = ent.get<nw::Common>();

    REQUIRE(common->resref == "cloth028");
    REQUIRE(item->properties.size() > 0);
    REQUIRE(item->model_type == nw::ItemModelType::armor);
    REQUIRE(common->locals.size() > 0);
}

TEST_CASE("item: load layered item", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/wduersc004.uti"));
    REQUIRE(ent.is_alive());

    auto item = ent.get<nw::Item>();
    auto common = ent.get<nw::Common>();

    REQUIRE(common->resref == "wduersc004");
    REQUIRE(item->properties.size() > 0);
    REQUIRE(item->model_type == nw::ItemModelType::composite);
}

TEST_CASE("item: load simple item", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/pl_aleu_shuriken.uti"));
    REQUIRE(ent.is_alive());

    auto item = ent.get<nw::Item>();
    auto common = ent.get<nw::Common>();

    REQUIRE(common->resref == "pl_aleu_shuriken");
    REQUIRE(item->properties.size() > 0);
    REQUIRE(item->model_type == nw::ItemModelType::simple);
}

TEST_CASE("item: to_json", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/cloth028.uti.json"};
    f << std::setw(4) << j;
}

TEST_CASE("item: json to and from", "[objects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent.is_alive());

    nlohmann::json j;
    nw::kernel::objects().serialize(ent, j, nw::SerializationProfile::blueprint);

    auto ent2 = nw::kernel::objects().deserialize(nw::ObjectType::item,
        j,
        nw::SerializationProfile::blueprint);
    REQUIRE(ent2.is_alive());

    nlohmann::json j2;
    nw::kernel::objects().serialize(ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);
}

TEST_CASE("item: gff round trip", "[ojbects]")
{
    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent.is_alive());

    nw::GffInputArchive g("test_data/user/development/cloth028.uti");
    REQUIRE(g.valid());

    nw::GffOutputArchive oa = nw::kernel::objects().serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/cloth028.uti");

    nw::GffInputArchive g2{"tmp/cloth028.uti"};
    REQUIRE(g2.valid());

    // Problem: local vars arent always saved in same order
    // REQUIRE(nw::gff_to_gffjson(g) == nw::gff_to_gffjson(g2));

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
