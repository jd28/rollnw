#include <catch2/catch.hpp>

#include <nw/resources/Resource.hpp>

#include <nlohmann/json.hpp>

using namespace std::literals;

TEST_CASE("Extensions", "[resources]")
{
    REQUIRE(nw::ResourceType::from_extension("2da") == nw::ResourceType::twoda);
    REQUIRE(nw::ResourceType::from_extension(".2da") == nw::ResourceType::twoda);
    REQUIRE(nw::ResourceType::from_extension("xxx") == nw::ResourceType::invalid);

    REQUIRE(nw::ResourceType::to_string(nw::ResourceType::txi) == "txi"s);
}

TEST_CASE("resref <-> JSON", "[resources]")
{
    nw::Resref r{"test"};
    nlohmann::json json_resref = r;
    REQUIRE(json_resref.is_string());
    nw::Resref r2 = json_resref.get<nw::Resref>();
    REQUIRE(r == r2);
    REQUIRE(r2.length());
}
