#include <catch2/catch.hpp>

#include <nw/components/Item.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("item: load armor item", "[objects]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent.is_alive());
    // auto light = nw::kernel::objects().load("nw_maarcl004", nw::ObjectType::item);
    // REQUIRE(light.is_alive());
    // auto medium = nw::kernel::objects().load("nw_maarcl040", nw::ObjectType::item);
    // REQUIRE(medium.is_alive());
    // auto heavy1 = nw::kernel::objects().load("x2_mdrowar030", nw::ObjectType::item);
    // REQUIRE(heavy1.is_alive());
    // auto heavy2 = nw::kernel::objects().load("x2_it_adaplate", nw::ObjectType::item);
    // REQUIRE(heavy2.is_alive());

    auto item = ent.get<nw::Item>();
    auto common = ent.get<nw::Common>();

    REQUIRE(common->resref == "cloth028");
    REQUIRE(item->properties.size() > 0);
    REQUIRE(item->model_type == nw::ItemModelType::armor);
    REQUIRE(common->locals.size() > 0);

    REQUIRE(nwn1::calculate_ac(ent) == 0);
    // REQUIRE(nwn1::calculate_ac(light) == 1);
    // REQUIRE(nwn1::calculate_ac(medium) == 5);
    // REQUIRE(nwn1::calculate_ac(heavy1) == 7);
    // REQUIRE(nwn1::calculate_ac(heavy2) == 8);

    nw::kernel::unload_module();
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

TEST_CASE("item: gff round trip", "[objects]")
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
