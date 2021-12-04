#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Waypoint.hpp>

TEST_CASE("Loading waypoint blueprint", "[objects]")
{
    nw::Gff g{"test_data/wp_behexit001.utw"};
    REQUIRE(g.valid());
    nw::Waypoint w{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(w.common()->resref == "wp_behexit001");
    REQUIRE(w.linked_to == "");
}
