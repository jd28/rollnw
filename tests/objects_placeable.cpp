#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Placeable.hpp>

TEST_CASE("Loading placeable", "[objects]")
{
    nw::Gff g{"test_data/arrowcorpse001.utp"};
    REQUIRE(g.valid());
    nw::Placeable p{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(p.resref == "arrowcorpse001");
    REQUIRE(p.appearance == 109);
    REQUIRE(!p.plot);
}
