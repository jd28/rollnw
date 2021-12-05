#include <catch2/catch.hpp>

#include <nw/objects/Item.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading armor item", "[objects]")
{
    nw::Gff g{"test_data/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.resref == "cloth028");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::armor);
    REQUIRE(i.local_data.size() > 0);
}

TEST_CASE("Loading layered item", "[objects]")
{
    nw::Gff g{"test_data/wduersc004.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.resref == "wduersc004");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::composite);
}

TEST_CASE("Loading simple item", "[objects]")
{
    nw::Gff g{"test_data/pl_aleu_shuriken.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.resref == "pl_aleu_shuriken");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::simple);
}
