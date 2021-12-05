#include <catch2/catch.hpp>

#include <nw/objects/Door.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading door", "[objects]")
{
    nw::Gff g{"test_data/door_ttr_002.utd"};
    REQUIRE(g.valid());
    nw::Door d{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(d.common()->resref == "door_ttr_002");
    REQUIRE(d.situated()->appearance == 0);
    REQUIRE(!d.situated()->plot);
    REQUIRE(!d.situated()->lock.locked);
}
