#include <catch2/catch_all.hpp>

#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>

#include <fstream>

TEST_CASE("model: parse ascii", "[model]")
{
    nw::model::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    REQUIRE(mdl.valid());
    REQUIRE(mdl.model.supermodel_name == "arfh_anims");
    REQUIRE(mdl.model.nodes.size() == 55);
    REQUIRE(mdl.model.nodes[0]->name == "alt_dfa19");
    REQUIRE(mdl.model.nodes[0]->children.size() == 4);
    REQUIRE(mdl.model.nodes[0]->children[0]->name == "rootdummy");

    auto [key, data] = mdl.model.nodes[0]->children[0]->get_controller(nw::model::ControllerType::Position);
    REQUIRE(key);
    REQUIRE(data.size() == 3);

    auto [key2, data2] = mdl.model.nodes[0]->children[0]->get_controller(nw::model::ControllerType::Orientation);
    REQUIRE(key2);
    REQUIRE(data2.size() == 4);

    REQUIRE(mdl.model.animations.size() == 12);
    REQUIRE(mdl.model.animations[0]->name == "kdbck");
    REQUIRE(mdl.model.animations[0]->events.size() == 16);
    REQUIRE(mdl.model.animations[0]->anim_root == "rootdummy");
}

TEST_CASE("model: parse ascii ee", "[model]")
{
    nw::model::Mdl mdl{"test_data/user/development/wplss_t_044.mdl"};
    REQUIRE(mdl.valid());
    REQUIRE(mdl.model.supermodel_name == "null");

    nw::model::Mdl mdl1{"test_data/user/development/c_mindflayer.mdl"};
    REQUIRE(mdl1.valid());

    nw::model::Mdl mdl2{"test_data/user/development/dag_grifflag03.mdl"};
    REQUIRE(mdl2.valid());

    nw::model::Mdl mdl3{"test_data/user/development/fx_caltrop.mdl"};
    REQUIRE(mdl3.valid());

    nw::model::Mdl mdl4{"test_data/user/development/gui_icon_7x5r.mdl"};
    REQUIRE(mdl4.valid());

    nw::model::Mdl mdl5{"test_data/user/development/it_torch_000.mdl"};
    REQUIRE(mdl5.valid());

    nw::model::Mdl mdl6{"test_data/user/development/pb_but_opts.mdl"};
    REQUIRE(mdl6.valid());

    nw::model::Mdl mdl7{"test_data/user/development/pfh0_robe020.mdl"};
    REQUIRE(mdl7.valid());

    nw::model::Mdl mdl8{"test_data/user/development/plc_fish01.mdl"};
    REQUIRE(mdl8.valid());

    nw::model::Mdl mdl9{"test_data/user/development/plc_palm02.mdl"};
    REQUIRE(mdl9.valid());

    nw::model::Mdl mdl10{"test_data/user/development/tcm02_p02_02.mdl"};
    REQUIRE(mdl10.valid());

    nw::model::Mdl mdl11{"test_data/user/development/tds_udoor_04.mdl"};
    REQUIRE(mdl11.valid());

    nw::model::Mdl mdl12{"test_data/user/development/tno01_v80_04.mdl"};
    REQUIRE(mdl12.valid());

    nw::model::Mdl mdl13{"test_data/user/development/tsw01_h12_01.mdl"};
    REQUIRE(mdl13.valid());

    nw::model::Mdl mdl14{"test_data/user/development/tss13_a35_01.mdl"};
    REQUIRE(mdl14.valid());

    nw::model::Mdl mdl15{"test_data/user/development/ctl_treebtn_spel.mdl"};
    REQUIRE(mdl15.valid());

    nw::model::Mdl mdl16{"test_data/user/development/tnp_tree_t11.mdl"};
    REQUIRE(mdl16.valid());

    nw::model::Mdl mdl17{"test_data/user/development/tcm02_f05_06.mdl"};
    REQUIRE(mdl17.valid());

    nw::model::Mdl mdl18{"test_data/user/development/tno01_v67_01.mdl"};
    REQUIRE(mdl18.valid());

    nw::model::Mdl mdl19{"test_data/user/development/ttf02_c01_15.mdl"};
    REQUIRE(mdl19.valid());

    nw::model::Mdl mdl20{"test_data/user/development/tni01_k02_01.mdl"};
    REQUIRE(mdl20.valid());

    nw::model::Mdl mdl21{"test_data/user/development/tbw01_c09_01.mdl"};
    REQUIRE(mdl21.valid());

    nw::model::Mdl mdl22{"test_data/user/development/fx_lanshard3.mdl"};
    REQUIRE(mdl22.valid());

    nw::model::Mdl mdl23{"test_data/user/development/c_orcus.mdl"};
    REQUIRE(mdl23.valid());

    nw::model::Mdl mdl24{"test_data/user/development/tno01_b90_03.mdl"};
    REQUIRE(mdl24.valid());

    nw::model::Mdl mdl25{"test_data/user/development/plc_cndl02.mdl"};
    REQUIRE(mdl25.valid());

    // [TODO] Fix failing test, not sure what "node pwk" is, if it's new or what.
    nw::model::Mdl mdl26{"test_data/user/development/ptm_candle07n.mdl"};
    REQUIRE_FALSE(mdl26.valid());

    nw::model::Mdl mdl27{"test_data/user/development/T_Door21.mdl"};
    REQUIRE(mdl27.valid());

    nw::model::Mdl mdl28{"test_data/user/development/ttf02_j09_02.mdl"};
    REQUIRE(mdl28.valid());

    nw::model::Mdl mdl29{"test_data/user/development/tts01_h02_03.mdl"};
    REQUIRE(mdl29.valid());

    // [TODO] Investigate whether all the controllers in here are obsolete
    // or really needed.  Only 3 models use them.
    nw::model::Mdl mdl30{"test_data/user/development/fx_step_stbrdg.mdl"};
    REQUIRE_FALSE(mdl30.valid());

    nw::model::Mdl mdl31{"test_data/user/development/vwp_flash_whiblu.mdl"};
    REQUIRE(mdl31.valid());
}

TEST_CASE("model: write to file", "[model]")
{
    nw::model::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    REQUIRE(mdl.valid());
    std::ofstream f{"tmp/alt_dfa19.mdl"};
    f << mdl;
    f.close();
}
