#include <catch2/catch.hpp>

#include "nw/resources/Resource.hpp"

using namespace std::literals;

TEST_CASE("Extensions", "[resources]")
{
    REQUIRE(nw::ResourceType::from_extension("2da") == nw::ResourceType::twoda);
    REQUIRE(nw::ResourceType::from_extension(".2da") == nw::ResourceType::twoda);
    REQUIRE(nw::ResourceType::from_extension("xxx") == nw::ResourceType::invalid);

    REQUIRE(nw::ResourceType::to_string(nw::ResourceType::txi) == "txi"s);
}
