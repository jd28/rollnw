#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Store.hpp>

TEST_CASE("Loading store blueprint", "[objects]")
{
    nw::Gff g{"test_data/storethief002.utm"};
    REQUIRE(g.valid());
    nw::Store s{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(s.common()->resref == "storethief002");
    REQUIRE(s.blackmarket);
    REQUIRE(s.blackmarket_markdown == 25);
    REQUIRE(s.weapons.items.size() > 0);
    REQUIRE(std::get<nw::Resref>(s.weapons.items[0].item) == "nw_wswdg001");
}
