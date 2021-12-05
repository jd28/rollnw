#include <catch2/catch.hpp>

#include <nw/objects/Sound.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading sound blueprint", "[objects]")
{
    nw::Gff g{"test_data/blue_bell.uts"};
    REQUIRE(g.valid());
    nw::Sound s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.common()->resref == "blue_bell");
}
