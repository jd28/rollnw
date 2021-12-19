#include <catch2/catch.hpp>

#include <nw/objects/Store.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("Loading store blueprint", "[objects]")
{
    nw::GffInputArchive g{"test_data/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.common()->resref == "storethief002");
    REQUIRE(s.blackmarket);
    REQUIRE(s.blackmarket_markdown == 25);
    REQUIRE(s.weapons.items.size() > 0);
    REQUIRE(std::get<nw::Resref>(s.weapons.items[0].item) == "nw_wswdg001");
}

TEST_CASE("store: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = s.to_json(nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/storethief002.utm.json"};
    f << std::setw(4) << j;
}

TEST_CASE("store: from_json", "[objects]")
{
    std::ifstream f{"tmp/storethief002.utm.json"};
    auto j = nlohmann::json::parse(f);
    nw::Store s;
    s.from_json(j, nw::SerializationProfile::blueprint);
    REQUIRE(s.common()->resref == "storethief002");
    REQUIRE(s.blackmarket);
    REQUIRE(s.blackmarket_markdown == 25);
}

TEST_CASE("store: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = s.to_json(nw::SerializationProfile::blueprint);
    nw::Store s2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = s2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}
