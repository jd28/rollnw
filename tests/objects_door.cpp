#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Door.hpp>

TEST_CASE("Loading door", "[objects]")
{
    nw::Gff g{"test_data/door_ttr_002.utd"};
    REQUIRE(g.valid());
    nw::Door d{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(d.resref == "door_ttr_002");
    REQUIRE(d.appearance == 0);
    REQUIRE(!d.plot);
    REQUIRE(!d.lock.locked);
}
