#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/resources/Directory.hpp>

using namespace nw;
using namespace std::literals;

TEST_CASE("Directory", "[containers]")
{
    std::filesystem::path p{"./test_data"};
    Directory d(p);
    REQUIRE(d.valid());
    REQUIRE(d.name() == "test_data");
    REQUIRE(d.path() == std::filesystem::canonical(p));

    auto ba = d.demand(Resource{"test"sv, ResourceType::nss});
    REQUIRE(ba.size());
    REQUIRE(ba.size() == std::filesystem::file_size("./test_data/test.nss"));
    REQUIRE(d.all().size() > 0);

    REQUIRE_THROWS(Directory{"./test_data/test.nss"});
    REQUIRE_THROWS(Directory{"./doesnotexist"});
}
