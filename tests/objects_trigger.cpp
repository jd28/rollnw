#include <catch2/catch.hpp>

#include <nw/objects/Trigger.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading trigger blueprint", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_spray_sewage.utt"};
    REQUIRE(g.valid());
    nw::Trigger t{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(t.common()->resref == "pl_spray_sewage");
    REQUIRE(t.portrait == 0);
    REQUIRE(t.loadscreen == 0);
    REQUIRE(t.on_enter == "pl_trig_sewage");
}
