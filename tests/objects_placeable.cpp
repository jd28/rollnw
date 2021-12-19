#include <catch2/catch.hpp>

#include <nw/objects/Placeable.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <nowide/fstream.hpp>

TEST_CASE("placeable: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/arrowcorpse001.utp"};
    REQUIRE(g.valid());
    nw::Placeable p{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(p.common()->resref == "arrowcorpse001");
    REQUIRE(p.appearance == 109);
    REQUIRE(!p.plot);
    REQUIRE(p.static_);
    REQUIRE(!p.useable);
}

TEST_CASE("placeable: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/arrowcorpse001.utp"};
    REQUIRE(g.valid());
    nw::Placeable p{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = p.to_json(nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/arrowcorpse001.utp.json"};
    f << std::setw(4) << j;
}

TEST_CASE("placeable: json to and from", "[objects]")
{
    nw::GffInputArchive g{"test_data/arrowcorpse001.utp"};
    REQUIRE(g.valid());
    nw::Placeable p{g.toplevel(), nw::SerializationProfile::blueprint};
    nlohmann::json j = p.to_json(nw::SerializationProfile::blueprint);
    nw::Placeable p2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = p2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}
