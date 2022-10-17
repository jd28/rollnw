#include <catch2/catch_all.hpp>

#include <nw/components/Item.hpp>
#include <nw/kernel/Objects.hpp>
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

    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent);
    auto light = nw::kernel::objects().load<nw::Item>("nw_maarcl004"sv);
    REQUIRE(light);
    auto medium = nw::kernel::objects().load<nw::Item>("nw_maarcl040"sv);
    REQUIRE(medium);
    auto heavy1 = nw::kernel::objects().load<nw::Item>("x2_mdrowar030"sv);
    REQUIRE(heavy1);
    auto heavy2 = nw::kernel::objects().load<nw::Item>("x2_it_adaplate"sv);
    REQUIRE(heavy2);

    REQUIRE(ent->common.resref == "cloth028");
    REQUIRE(ent->properties.size() > 0);
    REQUIRE(ent->model_type == nw::ItemModelType::armor);

    REQUIRE(nwn1::calculate_ac(ent) == 0);
    REQUIRE(nwn1::calculate_ac(light) == 1);
    REQUIRE(nwn1::calculate_ac(medium) == 5);
    REQUIRE(nwn1::calculate_ac(heavy1) == 7);
    REQUIRE(nwn1::calculate_ac(heavy2) == 8);

    nw::kernel::unload_module();
}

TEST_CASE("item: local vars", "[objects]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent);
    REQUIRE(ent->common.locals.size() > 0);
    REQUIRE(ent->common.locals.get_float("test1") == 1.5f);
    REQUIRE(ent->common.locals.get_float("doesn't have") == 0.0f);
    REQUIRE(ent->common.locals.get_string("stringtest") == "0");
    ent->common.locals.delete_string("stringtest");
    REQUIRE(ent->common.locals.get_string("stringtest") == "");

    ent->common.locals.set_string("test", "1");
    REQUIRE(ent->common.locals.get_string("test") == "1");
    ent->common.locals.set_int("test", 1);
    REQUIRE(ent->common.locals.get_int("test") == 1);
    ent->common.locals.set_float("test", 1.0);
    REQUIRE(ent->common.locals.get_float("test") == 1.0f);
}

TEST_CASE("item: load layered item", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/wduersc004.uti"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "wduersc004");
    REQUIRE(ent->properties.size() > 0);
    REQUIRE(ent->model_type == nw::ItemModelType::composite);
}

TEST_CASE("item: load simple item", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/pl_aleu_shuriken.uti"));
    REQUIRE(ent);

    REQUIRE(ent->common.resref == "pl_aleu_shuriken");
    REQUIRE(ent->properties.size() > 0);
    REQUIRE(ent->model_type == nw::ItemModelType::simple);
}

TEST_CASE("item: to_json", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Item::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/cloth028.uti.json"};
    f << std::setw(4) << j;
}

TEST_CASE("item: json to and from", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent);

    nlohmann::json j;
    nw::Item::serialize(ent, j, nw::SerializationProfile::blueprint);

    nw::Item ent2;
    REQUIRE(nw::Item::deserialize(&ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::Item::serialize(&ent2, j2, nw::SerializationProfile::blueprint);

    REQUIRE(j == j2);
}

TEST_CASE("item: gff round trip", "[objects]")
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    REQUIRE(ent);

    nw::Gff g("test_data/user/development/cloth028.uti");
    REQUIRE(g.valid());

    nw::GffBuilder oa = nw::Item::serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/cloth028.uti");

    nw::Gff g2{"tmp/cloth028.uti"};
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
