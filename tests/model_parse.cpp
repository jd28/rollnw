#include <catch2/catch_all.hpp>

#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>

#include <fstream>

TEST_CASE("model: parse ascii", "[model]")
{
    nw::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    REQUIRE(mdl.valid());
    REQUIRE(mdl.model.supermodel_name == "arfh_anims");
    REQUIRE(mdl.model.nodes.size() == 55);
    REQUIRE(mdl.model.nodes[0]->name == "alt_dfa19");
    REQUIRE(mdl.model.nodes[0]->children.size() == 4);
    REQUIRE(mdl.model.nodes[0]->children[0]->name == "rootdummy");

    auto [key, data] = mdl.model.nodes[0]->children[0]->get_controller(nw::MdlControllerType::Position);
    REQUIRE(key);
    REQUIRE(data.size() == 3);

    auto [key2, data2] = mdl.model.nodes[0]->children[0]->get_controller(nw::MdlControllerType::Orientation);
    REQUIRE(key2);
    REQUIRE(data2.size() == 4);

    REQUIRE(mdl.model.animations.size() == 12);
    REQUIRE(mdl.model.animations[0]->name == "kdbck");
    REQUIRE(mdl.model.animations[0]->events.size() == 16);
    REQUIRE(mdl.model.animations[0]->anim_root == "rootdummy");
}

TEST_CASE("model: parse ascii ee", "[model]")
{
    nw::Mdl mdl{"test_data/user/development/wplss_t_044.mdl"};
    REQUIRE(mdl.valid());
    REQUIRE(mdl.model.supermodel_name == "null");

    nw::Mdl mdl1{"test_data/user/development/c_mindflayer.mdl"};
    REQUIRE(mdl1.valid());

    nw::Mdl mdl2{"test_data/user/development/dag_grifflag03.mdl"};
    REQUIRE(mdl2.valid());

    nw::Mdl mdl3{"test_data/user/development/fx_caltrop.mdl"};
    REQUIRE(mdl3.valid());

    nw::Mdl mdl4{"test_data/user/development/gui_icon_7x5r.mdl"};
    REQUIRE(mdl4.valid());

    nw::Mdl mdl5{"test_data/user/development/it_torch_000.mdl"};
    REQUIRE(mdl5.valid());

    nw::Mdl mdl6{"test_data/user/development/pb_but_opts.mdl"};
    REQUIRE(mdl6.valid());

    nw::Mdl mdl7{"test_data/user/development/pfh0_robe020.mdl"};
    REQUIRE(mdl7.valid());

    nw::Mdl mdl8{"test_data/user/development/plc_fish01.mdl"};
    REQUIRE(mdl8.valid());

    nw::Mdl mdl9{"test_data/user/development/plc_palm02.mdl"};
    REQUIRE(mdl9.valid());

    nw::Mdl mdl10{"test_data/user/development/tcm02_p02_02.mdl"};
    REQUIRE(mdl10.valid());

    nw::Mdl mdl11{"test_data/user/development/tds_udoor_04.mdl"};
    REQUIRE(mdl11.valid());

    nw::Mdl mdl12{"test_data/user/development/tno01_v80_04.mdl"};
    REQUIRE(mdl12.valid());

    nw::Mdl mdl13{"test_data/user/development/tsw01_h12_01.mdl"};
    REQUIRE(mdl13.valid());

    nw::Mdl mdl14{"test_data/user/development/tss13_a35_01.mdl"};
    REQUIRE(mdl14.valid());

    nw::Mdl mdl15{"test_data/user/development/ctl_treebtn_spel.mdl"};
    REQUIRE(mdl15.valid());
}

TEST_CASE("model: write to file", "[model]")
{
    nw::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    REQUIRE(mdl.valid());
    std::ofstream f{"tmp/alt_dfa19.mdl"};
    f << mdl;
    f.close();
}
