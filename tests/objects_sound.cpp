#include <catch2/catch.hpp>

#include <nw/objects/Sound.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("sound: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/blue_bell.uts"};
    REQUIRE(g.valid());
    nw::Sound s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.common()->resref == "blue_bell");
}

TEST_CASE("sound: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/blue_bell.uts"};
    REQUIRE(g.valid());
    nw::Sound s{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = s.to_json(nw::SerializationProfile::blueprint);
    nw::Sound s2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = s2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(s2.common()->resref == "blue_bell");
    REQUIRE(j == j2);

    std::ofstream f{"tmp/blue_bell.uts.json"};
    f << std::setw(4) << j;
}
