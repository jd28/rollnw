#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Store.hpp>

TEST_CASE("Loading store blueprint", "[objects]")
{
    nw::Gff g{"test_data/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.resref == "storethief002");
}
