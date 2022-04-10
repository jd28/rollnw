#include <catch2/catch.hpp>

#include <nw/objects/Journal.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading journal", "[objects]")
{
    nw::GffInputArchive g{"test_data/user/scratch/module.jrl"};
    REQUIRE(g.valid());
    nw::Journal j{g.toplevel()};
    REQUIRE(j.categories.size() > 0);
}
