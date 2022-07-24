#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>

TEST_CASE("model: parse ascii", "[model]")
{
    nw::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    REQUIRE(mdl.valid());
    REQUIRE(mdl.model.supermodel_name == "arfh_anims");
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
