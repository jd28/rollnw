#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Trigger.hpp>

TEST_CASE("Loading trigger blueprint", "[objects]")
{
    nw::Gff g{"test_data/newtransition001.utt"};
    REQUIRE(g.valid());
    nw::Trigger t{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(t.resref == "newtransition001");
}
