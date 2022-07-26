#include <catch2/catch.hpp>

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
}

TEST_CASE("model: write to file", "[model]")
{
    nw::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    REQUIRE(mdl.valid());
    std::ofstream f{"tmp/alt_dfa19.mdl"};
    f << mdl;
    f.close();
}
