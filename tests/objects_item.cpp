#include <catch2/catch.hpp>

#include <nw/objects/Item.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("item: load armor item", "[objects]")
{
    nw::GffInputArchive g{"test_data/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "cloth028");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::armor);
    REQUIRE(i.common()->local_data.size() > 0);
}

TEST_CASE("item: load layered item", "[objects]")
{
    nw::GffInputArchive g{"test_data/wduersc004.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "wduersc004");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::composite);
}

TEST_CASE("item: load simple item", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_aleu_shuriken.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "pl_aleu_shuriken");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::simple);
}

TEST_CASE("item: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(i.common()->resref == "cloth028");
    REQUIRE(i.properties.size() > 0);
    REQUIRE(i.model_type == nw::ItemModelType::armor);
    REQUIRE(i.common()->local_data.size() > 0);

    nlohmann::json j = i.to_json(nw::SerializationProfile::blueprint);

    nw::Item it2;
    it2.from_json(j, nw::SerializationProfile::blueprint);
    REQUIRE(it2.common()->local_data.get_local_string("stringtest") == "0");

    std::ofstream f{"tmp/cloth028.uti.json"};
    f << std::setw(4) << j;
}

TEST_CASE("item: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/cloth028.uti"};
    nw::Item i{g.toplevel(), nw::SerializationProfile::blueprint};
    nlohmann::json j = i.to_json(nw::SerializationProfile::blueprint);
    nw::Item it2{j, nw::SerializationProfile::blueprint};
    REQUIRE(it2.common()->local_data.get_local_string("stringtest") == "0");
    nlohmann::json j2 = it2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}
