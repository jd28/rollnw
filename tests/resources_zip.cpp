#include <catch2/catch_all.hpp>

#include <nw/log.hpp>
#include <nw/resources/Zip.hpp>
#include <nw/util/platform.hpp>

#include <filesystem>
#include <regex>

namespace fs = std::filesystem;
using namespace std::literals;

TEST_CASE("Load zip", "[resources]")
{
    nw::Zip z{"test_data/user/modules/module_as_zip.zip"};
    REQUIRE(z.valid());
    REQUIRE(z.name() == "module_as_zip.zip");
    REQUIRE(z.extract(std::regex(".*"), "tmp") == 23);

    REQUIRE_THROWS(nw::Zip{"test_data/user/development/non-extant.zip"});
}
