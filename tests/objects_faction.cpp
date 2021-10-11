#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Faction.hpp>

TEST_CASE("Loading faction", "[objects]")
{
    nw::Gff g{"test_data/Repute.fac"};
    REQUIRE(g.valid());
    nw::Faction f{g.toplevel()};
    REQUIRE(f.factions.size() >= 4);
    REQUIRE(f.reputations.size() > 0);
    REQUIRE(f.factions[0].name == "PC");
    REQUIRE(f.factions[1].name == "Hostile");
    REQUIRE(f.factions[2].name == "Commoner");
    REQUIRE(f.factions[3].name == "Merchant");
}
