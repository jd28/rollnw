#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Creature.hpp>

TEST_CASE("Loading creature", "[objects]")
{
    nw::Gff g{"test_data/nw_chicken.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(c.resref == "nw_chicken");
}
