#include <catch2/catch_all.hpp>

#include <nw/log.hpp>
#include <nw/resources/Directory.hpp>

using namespace nw;
using namespace std::literals;

TEST_CASE("Directory", "[containers]")
{
    std::filesystem::path p{"./test_data/user/development/"};
    Directory d(p);
    REQUIRE(d.valid());
    REQUIRE(d.name() == "development");
    REQUIRE(d.path() == std::filesystem::canonical(p));

    REQUIRE(d.contains(Resource{"test"sv, ResourceType::nss}));
    auto ba = d.demand(Resource{"test"sv, ResourceType::nss});
    REQUIRE(ba.size());
    REQUIRE(ba.size() == std::filesystem::file_size("./test_data/user/development/test.nss"));
    REQUIRE(d.all().size() > 0);

    REQUIRE_FALSE(Directory{"./test_data/user/development/test.nss"}.valid());
    REQUIRE_FALSE(Directory{"./doesnotexist"}.valid());

    auto rd = d.stat(Resource{"test"sv, ResourceType::nss});
    REQUIRE(ba.size() == rd.size);
}
