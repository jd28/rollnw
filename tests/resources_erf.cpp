#include <catch2/catch.hpp>

#include "nw/log.hpp"
#include "nw/resources/Erf.hpp"

using namespace nw;
using namespace std::literals;

TEST_CASE("Erf loading", "[containers]")
{
    Erf e("test_data/DockerDemo.mod");
    REQUIRE(e.size() > 0);
    REQUIRE(e.all().size() > 0);
}

TEST_CASE("Erf demanding", "[containers]")
{
    Erf e("test_data/DockerDemo.mod");
    auto ba = e.demand({"module"sv, ResourceType::ifo});
    REQUIRE(ba.size() > 0);
}

TEST_CASE("Erf extracting", "[containers]")
{
    Erf e("test_data/DockerDemo.mod");
    auto ba = e.demand({"module"sv, ResourceType::ifo});
    REQUIRE(e.extract(std::regex("module.ifo"), "tmp/") == 1);
    REQUIRE(ba.size() == std::filesystem::file_size("tmp/module.ifo"));
}
