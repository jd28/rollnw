#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/ResourceLocator.hpp>

using namespace std::literals;

TEST_CASE("resource-locator-contains", "[resources]")
{
    nw::ResourceLocator loc;
    nw::Erf e("test_data/DockerDemo.mod");
    REQUIRE(loc.add_container(&e, false));
    REQUIRE(loc.contains({"module"sv, nw::ResourceType::ifo}));
    REQUIRE(loc.size() == e.size());
}

TEST_CASE("resource-locator-contains-2", "[resources]")
{
    nw::ResourceLocator loc;
    REQUIRE(loc.add_container("test_data/DockerDemo.mod"));
    REQUIRE(loc.add_container("test_data/temp0.zip"));
    REQUIRE(loc.contains({"module"sv, nw::ResourceType::ifo}));
    REQUIRE(loc.contains({"test_area"sv, nw::ResourceType::are}));
    REQUIRE_FALSE(loc.add_container("non-extent"));
    REQUIRE(loc.extract(std::regex(".*"), "tmp") == 37);
}
