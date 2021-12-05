#include <catch2/catch.hpp>

#include <nw/objects/Encounter.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading encounter blueprint", "[objects]")
{
    nw::GffInputArchive g{"test_data/boundelementallo.ute"};
    REQUIRE(g.valid());
    nw::Encounter e{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(e.common()->resref == "boundelementallo");
    REQUIRE(!!e.player_only);
    REQUIRE(e.on_entered == "");
    REQUIRE(e.creatures.size() > 0);
    REQUIRE(e.creatures[0].resref == "tyn_bound_acid_l");
}
