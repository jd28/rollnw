#include <gtest/gtest.h>

#include <nw/kernel/Resources.hpp>
#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>

#include <fstream>

using namespace std::literals;
namespace nwk = nw::kernel;

TEST(Mdl, ParseASCII)
{
    nw::model::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    EXPECT_TRUE(mdl.valid());
    EXPECT_EQ(mdl.model.supermodel_name, "arfh_anims");
    EXPECT_EQ(mdl.model.nodes.size(), 55);
    EXPECT_EQ(mdl.model.nodes[0]->name, "alt_dfa19");
    EXPECT_EQ(mdl.model.nodes[0]->children.size(), 4);
    EXPECT_EQ(mdl.model.nodes[0]->children[0]->name, "rootdummy");

    auto val = mdl.model.nodes[0]->children[0]->get_controller(nw::model::ControllerType::Position);
    EXPECT_TRUE(val.key);
    EXPECT_EQ(val.data.size(), 3);

    auto val2 = mdl.model.nodes[0]->children[0]->get_controller(nw::model::ControllerType::Orientation);
    EXPECT_TRUE(val2.key);
    EXPECT_EQ(val2.data.size(), 4);

    EXPECT_EQ(mdl.model.animations.size(), 12);
    EXPECT_EQ(mdl.model.animations[0]->name, "kdbck");
    EXPECT_EQ(mdl.model.animations[0]->events.size(), 16);
    EXPECT_EQ(mdl.model.animations[0]->anim_root, "rootdummy");

    auto anim = mdl.model.find_animation("kdbck");
    EXPECT_TRUE(anim);
    auto root_re = std::regex(".*rootdummy.*", std::regex_constants::icase);
    auto anode = anim->find(root_re);
    EXPECT_TRUE(anode);
    auto val3 = anode->get_controller(nw::model::ControllerType::Orientation, true);
    EXPECT_TRUE(val3.key);
    EXPECT_EQ(val3.time[1], 0.1f);
    EXPECT_EQ(size_t(val3.key->rows), val3.time.size());
    EXPECT_EQ(val3.data.size(), size_t(val3.key->rows * val3.key->columns));
}

TEST(Mdl, ParseBinary)
{
    nw::model::Mdl mdl{"test_data/user/development/c_bodak.mdl"};
    EXPECT_TRUE(mdl.valid());
    EXPECT_EQ(mdl.model.supermodel_name, "a_ba");
    EXPECT_EQ(mdl.model.nodes[2]->children.size(), 2);
    // EXPECT_TRUE(mdl.model.supermodel);
}

TEST(Mdl, ParseASCII2)
{
    nw::model::Mdl mdl{"test_data/user/development/wplss_t_044.mdl"};
    EXPECT_TRUE(mdl.valid());
    EXPECT_EQ(mdl.model.supermodel_name, "null");

    auto root_re = std::regex(".*rootdummy.*", std::regex_constants::icase);
    nw::model::Mdl mdl1{"test_data/user/development/c_mindflayer.mdl"};
    EXPECT_TRUE(mdl1.valid());
    EXPECT_TRUE(mdl1.model.find(root_re));

    nw::model::Mdl mdl2{"test_data/user/development/dag_grifflag03.mdl"};
    EXPECT_TRUE(mdl2.valid());

    nw::model::Mdl mdl3{"test_data/user/development/fx_caltrop.mdl"};
    EXPECT_TRUE(mdl3.valid());

    nw::model::Mdl mdl4{"test_data/user/development/gui_icon_7x5r.mdl"};
    EXPECT_TRUE(mdl4.valid());

    nw::model::Mdl mdl5{"test_data/user/development/it_torch_000.mdl"};
    EXPECT_TRUE(mdl5.valid());

    nw::model::Mdl mdl6{"test_data/user/development/pb_but_opts.mdl"};
    EXPECT_TRUE(mdl6.valid());

    nw::model::Mdl mdl7{"test_data/user/development/pfh0_robe020.mdl"};
    EXPECT_TRUE(mdl7.valid());

    nw::model::Mdl mdl8{"test_data/user/development/plc_fish01.mdl"};
    EXPECT_TRUE(mdl8.valid());

    nw::model::Mdl mdl9{"test_data/user/development/plc_palm02.mdl"};
    EXPECT_TRUE(mdl9.valid());

    nw::model::Mdl mdl10{"test_data/user/development/tcm02_p02_02.mdl"};
    EXPECT_TRUE(mdl10.valid());

    nw::model::Mdl mdl11{"test_data/user/development/tds_udoor_04.mdl"};
    EXPECT_TRUE(mdl11.valid());

    nw::model::Mdl mdl12{"test_data/user/development/tno01_v80_04.mdl"};
    EXPECT_TRUE(mdl12.valid());

    nw::model::Mdl mdl13{"test_data/user/development/tsw01_h12_01.mdl"};
    EXPECT_TRUE(mdl13.valid());

    nw::model::Mdl mdl14{"test_data/user/development/tss13_a35_01.mdl"};
    EXPECT_TRUE(mdl14.valid());

    nw::model::Mdl mdl15{"test_data/user/development/ctl_treebtn_spel.mdl"};
    EXPECT_TRUE(mdl15.valid());

    nw::model::Mdl mdl16{"test_data/user/development/tnp_tree_t11.mdl"};
    EXPECT_TRUE(mdl16.valid());

    nw::model::Mdl mdl17{"test_data/user/development/tcm02_f05_06.mdl"};
    EXPECT_TRUE(mdl17.valid());

    nw::model::Mdl mdl18{"test_data/user/development/tno01_v67_01.mdl"};
    EXPECT_TRUE(mdl18.valid());

    nw::model::Mdl mdl19{"test_data/user/development/ttf02_c01_15.mdl"};
    EXPECT_TRUE(mdl19.valid());

    nw::model::Mdl mdl20{"test_data/user/development/tni01_k02_01.mdl"};
    EXPECT_TRUE(mdl20.valid());

    nw::model::Mdl mdl21{"test_data/user/development/tbw01_c09_01.mdl"};
    EXPECT_FALSE(mdl21.valid()); // Weird bitmap value

    nw::model::Mdl mdl22{"test_data/user/development/fx_lanshard3.mdl"};
    EXPECT_TRUE(mdl22.valid());

    nw::model::Mdl mdl23{"test_data/user/development/c_orcus.mdl"};
    EXPECT_TRUE(mdl23.valid());

    nw::model::Mdl mdl24{"test_data/user/development/tno01_b90_03.mdl"};
    EXPECT_TRUE(mdl24.valid());

    nw::model::Mdl mdl25{"test_data/user/development/plc_cndl02.mdl"};
    EXPECT_TRUE(mdl25.valid());

    // [TODO] Fix failing test, not sure what "node pwk" is, if it's new or what.
    nw::model::Mdl mdl26{"test_data/user/development/ptm_candle07n.mdl"};
    EXPECT_FALSE(mdl26.valid());

    nw::model::Mdl mdl27{"test_data/user/development/T_Door21.mdl"};
    EXPECT_TRUE(mdl27.valid());

    nw::model::Mdl mdl28{"test_data/user/development/ttf02_j09_02.mdl"};
    EXPECT_TRUE(mdl28.valid());

    nw::model::Mdl mdl29{nw::ResourceData::from_file("test_data/user/development/tts01_h02_03.mdl")};
    EXPECT_TRUE(mdl29.valid());

    // [TODO] Investigate whether all the controllers in here are obsolete
    // or really needed.  Only 3 models use them.
    nw::model::Mdl mdl30{"test_data/user/development/fx_step_stbrdg.mdl"};
    EXPECT_FALSE(mdl30.valid());

    nw::model::Mdl mdl31{"test_data/user/development/vwp_flash_whiblu.mdl"};
    EXPECT_TRUE(mdl31.valid());
}

TEST(Model, Animations)
{
    nw::model::Mdl mdl{"test_data/user/development/a_ba.mdl"};
    EXPECT_TRUE(mdl.valid());
    EXPECT_EQ(mdl.model.animations[0]->name, "walk");
    EXPECT_EQ(mdl.model.animations[1]->name, "walk_shieldl");

    nw::model::Mdl mdl1{"test_data/user/development/c_bodak.mdl"};
    EXPECT_TRUE(mdl1.valid());
    EXPECT_TRUE(mdl1.model.supermodel);

    std::set<std::string> animation_names;
    for (const auto& it : mdl1.model.supermodel->model.animations) {
        animation_names.insert(it->name);
    }
    EXPECT_EQ(animation_names.size(), mdl1.model.supermodel->model.animations.size());

    nw::model::Mdl mdl2{"test_data/user/development/a_fa.mdl"};
    EXPECT_TRUE(mdl2.valid());
    EXPECT_GT(mdl.model.animations.size(), 0);
    auto walk = mdl.model.find_animation("walk");
    EXPECT_TRUE(walk);
    auto node = walk->find(std::regex("torso_g"));
    EXPECT_TRUE(node);
    auto cont = node->get_controller(nw::model::ControllerType::Orientation, true);
    EXPECT_NEAR(cont.data[0], -0.0916844, 0.0001);
}

TEST(Model, Skin)
{
    nw::model::Mdl mdl1{"test_data/user/development/c_satyr.mdl"};
    EXPECT_TRUE(mdl1.valid());
    auto node = mdl1.model.find(std::regex("bodymesh_g", std::regex_constants::icase));
    EXPECT_TRUE(node);
    auto skin = static_cast<nw::model::SkinNode*>(node);
    EXPECT_GT(skin->vertices.size(), 0);
    EXPECT_EQ(skin->bone_nodes[skin->vertices[2].bones[0]], 22);
    EXPECT_TRUE(nw::string::icmp("head_g", mdl1.model.nodes[skin->bone_nodes[skin->vertices[2].bones[0]]]->name));
    EXPECT_NEAR(skin->vertices[2].weights[0], 0.649999976f, 0.0001);
    EXPECT_EQ(skin->bone_nodes[skin->vertices[2].bones[1]], 21);
    EXPECT_TRUE(nw::string::icmp("neck_g", mdl1.model.nodes[skin->bone_nodes[skin->vertices[2].bones[1]]]->name));
    EXPECT_NEAR(skin->vertices[2].weights[1], 0.349999994f, 0.0001);
    EXPECT_EQ(skin->vertices[2].bones[2], 0);
    EXPECT_NEAR(skin->vertices[2].weights[2], 0.0f, 0.0001);
    EXPECT_EQ(skin->vertices[2].bones[3], 0);
    EXPECT_NEAR(skin->vertices[2].weights[3], 0.0f, 0.0001);

    nw::model::Mdl mdl2{"test_data/user/development/c_satyrarcher.mdl"};
    EXPECT_TRUE(mdl2.valid());
    auto node2 = mdl2.model.find(std::regex("bodymesh_g", std::regex_constants::icase));
    EXPECT_TRUE(node2);
    auto skin2 = static_cast<nw::model::SkinNode*>(node2);

    EXPECT_GT(skin2->vertices.size(), 0);
    EXPECT_EQ(skin2->vertices[340].bones[0], 8);
    EXPECT_TRUE(nw::string::icmp("Lbicep_g", mdl1.model.nodes[skin2->bone_nodes[skin2->vertices[340].bones[0]]]->name));
    EXPECT_NEAR(skin2->vertices[340].weights[0], 0.696788013f, 0.0001);
    EXPECT_EQ(skin2->vertices[340].bones[1], 3);
    EXPECT_TRUE(nw::string::icmp("torso_g", mdl1.model.nodes[skin2->bone_nodes[skin2->vertices[340].bones[1]]]->name));
    EXPECT_NEAR(skin2->vertices[340].weights[1], 0.188104004f, 0.0001);
    EXPECT_EQ(skin2->vertices[340].bones[2], 9);
    EXPECT_TRUE(nw::string::icmp("Lforearm_g", mdl1.model.nodes[skin2->bone_nodes[skin2->vertices[340].bones[2]]]->name));
    EXPECT_NEAR(skin2->vertices[340].weights[2], 0.115108997f, 0.0001);
    EXPECT_EQ(skin2->vertices[340].bones[3], 0);
    EXPECT_NEAR(skin2->vertices[340].weights[3], 0.0f, 0.0001);
}

// TEST("model: write to file", "[model]")
// {
//     nw::model::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
//     EXPECT_TRUE(mdl.valid());
//     std::ofstream f{"tmp/alt_dfa19.mdl"};
//     f << mdl;
//     f.close();
// }
