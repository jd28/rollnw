#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Sound.hpp>

TEST_CASE("Loading sound blueprint", "[objects]")
{
    nw::Gff g{"test_data/blue_bell.uts"};
    REQUIRE(g.valid());
    nw::Sound s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.resref == "blue_bell");
}
