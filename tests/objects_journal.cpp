#include <catch2/catch.hpp>

#include <nw/formats/Gff.hpp>
#include <nw/objects/Journal.hpp>

TEST_CASE("Loading journal", "[objects]")
{
    nw::Gff g{"test_data/module.jrl"};
    REQUIRE(g.valid());
    nw::Journal j{g.toplevel()};
    REQUIRE(j.categories.size() > 0);
}
