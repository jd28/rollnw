#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Encounter.hpp>

TEST_CASE("Loading encounter blueprint", "[objects]")
{
    nw::Gff g{"test_data/boundelementallo.ute"};
    REQUIRE(g.valid());
    nw::Encounter e{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(e.resref == "boundelementallo");
}
