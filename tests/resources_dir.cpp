#include <catch2/catch.hpp>

#include "nw/log.hpp"
#include "nw/resources/Directory.hpp"

using namespace nw;
using namespace std::literals;

TEST_CASE("Directory", "[containers]")
{
    Directory d("./test_data");
    auto ba = d.demand(Resource{"test"sv, ResourceType::nss});
    REQUIRE(ba.size());
    REQUIRE(ba.size() == std::filesystem::file_size("./test_data/test.nss"));
    REQUIRE(d.all().size() > 0);
}
