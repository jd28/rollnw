#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Trigger.hpp>

TEST_CASE("Loading trigger blueprint", "[objects]")
{
    nw::Gff g{"test_data/pl_spray_sewage.utt"};
    REQUIRE(g.valid());
    nw::Trigger t{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(t.resref == "pl_spray_sewage");
    REQUIRE(t.portrait == 0);
    REQUIRE(t.loadscreen == 0);
    REQUIRE(t.on_enter == "pl_trig_sewage");
}
