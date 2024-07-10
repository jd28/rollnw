#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Item.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using namespace std::literals;

TEST(Item, Colors)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto heavy1 = nw::kernel::objects().load<nw::Item>("x2_mdrowar030"sv);
    EXPECT_TRUE(heavy1);
    auto plt_colors = heavy1->model_to_plt_colors();
    EXPECT_EQ(plt_colors.data[nw::plt_layer_metal1], 3);
    EXPECT_EQ(plt_colors.data[nw::plt_layer_metal2], 25);

    nw::kernel::unload_module();
}

TEST(Item, GffDeserializeArmor)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto name = nw::Item::get_name_from_file(fs::path("test_data/user/development/cloth028.uti"));
    EXPECT_EQ(name, "Adept's Tunic");

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
    ent->common.locals.clear("test", nw::LocalVarType::float_);
    EXPECT_EQ(ent->common.locals.get_float("test"), 0.0f);

    ent->common.locals.set_string("test", "1");
    for (auto type : {nw::LocalVarType::integer,
             nw::LocalVarType::float_,
             nw::LocalVarType::object,
             nw::LocalVarType::location,
             nw::LocalVarType::string}) {
        ent->common.locals.clear_all(type);
    }
    EXPECT_EQ(ent->common.locals.get_string("test"), "");
    EXPECT_EQ(ent->common.locals.size(), 0);
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
    f.close();

    auto name = nw::Item::get_name_from_file(fs::path("tmp/cloth028.uti.json"));
    EXPECT_EQ(name, "Adept's Tunic");
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

TEST(Item, Icon)
{
    // Can't run these tests against the docker/server distro since it strips models,
    // textures, etc.
#if 0
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    // Compound
    auto item1 = nw::kernel::objects().load<nw::Item>("nw_wswbs001"sv);
    EXPECT_TRUE(item1);
    auto icon1_1 = item1->get_icon_by_part(nw::ItemModelParts::model1);
    EXPECT_TRUE(icon1_1);
    EXPECT_TRUE(icon1_1->valid());
    EXPECT_TRUE(icon1_1->write_to("tmp/wswbs001_b.png"));
    delete icon1_1;

    auto icon1_2 = item1->get_icon_by_part(nw::ItemModelParts::model2);
    EXPECT_TRUE(icon1_2);
    EXPECT_TRUE(icon1_2->valid());
    EXPECT_TRUE(icon1_2->write_to("tmp/wswbs001_m.png"));
    delete icon1_2;

    auto icon1_3 = item1->get_icon_by_part(nw::ItemModelParts::model3);
    EXPECT_TRUE(icon1_3);
    EXPECT_TRUE(icon1_3->valid());
    EXPECT_TRUE(icon1_3->write_to("tmp/wswbs001_t.png"));
    delete icon1_3;

    // Simple
    auto item2 = nw::kernel::objects().load<nw::Item>("x2_smchaosshield"sv);
    EXPECT_TRUE(item2);
    auto icon2_1 = item2->get_icon_by_part();
    EXPECT_TRUE(icon2_1);
    EXPECT_TRUE(icon2_1->valid());
    EXPECT_TRUE(icon2_1->write_to("tmp/x2_smchaosshield.png"));
    delete icon2_1;

    // Layered
    auto item3 = nw::kernel::objects().load<nw::Item>("x2_helm_002"sv);
    EXPECT_TRUE(item3);
    auto icon3_1 = item3->get_icon_by_part();
    EXPECT_TRUE(icon3_1);
    EXPECT_TRUE(icon3_1->valid());
    EXPECT_TRUE(icon3_1->write_to("tmp/x2_helm_002.png"));
    delete icon3_1;

    auto item4 = nw::kernel::objects().load<nw::Item>("x2_it_drowcl001"sv);
    EXPECT_TRUE(item4);
    auto icon4_1 = item4->get_icon_by_part();
    EXPECT_TRUE(icon4_1);
    EXPECT_TRUE(icon4_1->valid());
    EXPECT_TRUE(icon4_1->write_to("tmp/x2_it_drowcl001.png"));
    delete icon4_1;

    // Armor
    auto item5 = nw::kernel::objects().load<nw::Item>("x2_it_adaplate"sv);
    EXPECT_TRUE(item5);
    auto icon5_1 = item5->get_icon_by_part(nw::ItemModelParts::armor_torso);
    EXPECT_TRUE(icon5_1);
    EXPECT_TRUE(icon5_1->valid());
    EXPECT_TRUE(icon5_1->write_to("tmp/x2_it_adaplate.png"));
    delete icon5_1;

    auto icon5_2 = item5->get_icon_by_part(nw::ItemModelParts::armor_torso, true);
    EXPECT_TRUE(icon5_2);
    EXPECT_TRUE(icon5_2->valid());
    EXPECT_TRUE(icon5_2->write_to("tmp/x2_it_adaplatef.png"));
    delete icon5_2;

    nw::kernel::unload_module();
#endif
}
