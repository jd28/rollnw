#include <catch2/catch.hpp>

#include <nw/objects/Encounter.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("encounter: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/boundelementallo.ute"};
    REQUIRE(g.valid());
    nw::Encounter e{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(e.common()->resref == "boundelementallo");
    REQUIRE(!!e.player_only);
    REQUIRE(e.scripts.on_entered == "");
    REQUIRE(e.creatures.size() > 0);
    REQUIRE(e.creatures[0].resref == "tyn_bound_acid_l");
}

TEST_CASE("encounter: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/boundelementallo.ute"};
    nw::Encounter e{g.toplevel(), nw::SerializationProfile::blueprint};

    // REQUIRE(e.valid());

    nlohmann::json j = e.to_json(nw::SerializationProfile::blueprint);

    std::ofstream f{"tmp/boundelementallo.ute.json"};
    f << std::setw(4) << j;
}

TEST_CASE("encounter: json back and forth", "[objects]")
{
    nw::GffInputArchive g{"test_data/boundelementallo.ute"};
    nw::Encounter e{g.toplevel(), nw::SerializationProfile::blueprint};
    nlohmann::json j = e.to_json(nw::SerializationProfile::blueprint);
    nw::Encounter e2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = e2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);
}
