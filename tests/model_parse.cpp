#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/model/Mdl.hpp>

TEST_CASE("model: parse ascii", "[model]")
{
    nw::Mdl mdl{"test_data/user/development/alt_dfa19.mdl"};
    REQUIRE(mdl.valid());
    REQUIRE(mdl.model.supermodel_name == "arfh_anims");
}

TEST_CASE("model: parse ascii ee", "[model]")
{
    nw::Mdl mdl{"test_data/user/development/wplss_t_044.mdl"};
    REQUIRE(mdl.valid());
    REQUIRE(mdl.model.supermodel_name == "null");
}
