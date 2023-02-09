#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Item.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Item, GffDeserializeArmor)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_TRUE(ent);
    auto light = nw::kernel::objects().load<nw::Item>("nw_maarcl004"sv);
    EXPECT_TRUE(light);
    auto medium = nw::kernel::objects().load<nw::Item>("nw_maarcl040"sv);
    EXPECT_TRUE(medium);
    auto heavy1 = nw::kernel::objects().load<nw::Item>("x2_mdrowar030"sv);
    EXPECT_TRUE(heavy1);
    auto heavy2 = nw::kernel::objects().load<nw::Item>("x2_it_adaplate"sv);
    EXPECT_TRUE(heavy2);

    EXPECT_EQ(ent->common.resref, "cloth028");
    EXPECT_TRUE(ent->properties.size() > 0);
    EXPECT_EQ(ent->model_type, nw::ItemModelType::armor);

    EXPECT_EQ(nwn1::calculate_item_ac(ent), 0);
    EXPECT_EQ(nwn1::calculate_item_ac(light), 1);
    EXPECT_EQ(nwn1::calculate_item_ac(medium), 5);
    EXPECT_EQ(nwn1::calculate_item_ac(heavy1), 7);
    EXPECT_EQ(nwn1::calculate_item_ac(heavy2), 8);

    nw::kernel::unload_module();
}

TEST(Item, LocalVariables)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_TRUE(ent);
    EXPECT_GT(ent->common.locals.size(), 0);
    EXPECT_EQ(ent->common.locals.get_float("test1"), 1.5f);
    EXPECT_EQ(ent->common.locals.get_float("doesn't have"), 0.0f);
    EXPECT_EQ(ent->common.locals.get_string("stringtest"), "0");
    ent->common.locals.delete_string("stringtest");
    EXPECT_EQ(ent->common.locals.get_string("stringtest"), "");

    ent->common.locals.set_string("test", "1");
    EXPECT_EQ(ent->common.locals.get_string("test"), "1");
    ent->common.locals.set_int("test", 1);
    EXPECT_EQ(ent->common.locals.get_int("test"), 1);
    ent->common.locals.set_float("test", 1.0);
    EXPECT_EQ(ent->common.locals.get_float("test"), 1.0f);
}

TEST(Item, GffDeserializeLayered)
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/wduersc004.uti"));
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->common.resref, "wduersc004");
    EXPECT_GT(ent->properties.size(), 0);
    EXPECT_EQ(ent->model_type, nw::ItemModelType::composite);
}

TEST(Item, GffDeserializeSimple)
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/pl_aleu_shuriken.uti"));
    EXPECT_TRUE(ent);

    EXPECT_EQ(ent->common.resref, "pl_aleu_shuriken");
    EXPECT_GT(ent->properties.size(), 0);
    EXPECT_EQ(ent->model_type, nw::ItemModelType::simple);
}

TEST(Item, JsonSerialize)
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Item::serialize(ent, j, nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/cloth028.uti.json"};
    f << std::setw(4) << j;
}

TEST(Item, JsonRoundTrip)
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_TRUE(ent);

    nlohmann::json j;
    nw::Item::serialize(ent, j, nw::SerializationProfile::blueprint);

    nw::Item ent2;
    EXPECT_TRUE(nw::Item::deserialize(&ent2, j, nw::SerializationProfile::blueprint));

    nlohmann::json j2;
    nw::Item::serialize(&ent2, j2, nw::SerializationProfile::blueprint);

    EXPECT_EQ(j, j2);
}

TEST(Item, GffRoundTrip)
{
    auto ent = nw::kernel::objects().load<nw::Item>(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_TRUE(ent);

    nw::Gff g("test_data/user/development/cloth028.uti");
    EXPECT_TRUE(g.valid());

    nw::GffBuilder oa = serialize(ent, nw::SerializationProfile::blueprint);
    oa.write_to("tmp/cloth028.uti");

    nw::Gff g2{"tmp/cloth028.uti"};
    EXPECT_TRUE(g2.valid());

    // Problem: local vars arent always saved in same order
    // EXPEEQRUE(nw::gff_to_gffjson(g) , nw::gff_to_gffjson(g2));

    EXPECT_EQ(oa.header.struct_offset, g.head_->struct_offset);
    EXPECT_EQ(oa.header.struct_count, g.head_->struct_count);
    EXPECT_EQ(oa.header.field_offset, g.head_->field_offset);
    EXPECT_EQ(oa.header.field_count, g.head_->field_count);
    EXPECT_EQ(oa.header.label_offset, g.head_->label_offset);
    EXPECT_EQ(oa.header.label_count, g.head_->label_count);
    EXPECT_EQ(oa.header.field_data_offset, g.head_->field_data_offset);
    EXPECT_EQ(oa.header.field_data_count, g.head_->field_data_count);
    EXPECT_EQ(oa.header.field_idx_offset, g.head_->field_idx_offset);
    EXPECT_EQ(oa.header.field_idx_count, g.head_->field_idx_count);
    EXPECT_EQ(oa.header.list_idx_offset, g.head_->list_idx_offset);
    EXPECT_EQ(oa.header.list_idx_count, g.head_->list_idx_count);
}
