#include <catch2/catch.hpp>

#include <nw/log.hpp>
#include <nw/resources/Zip.hpp>
#include <nw/util/platform.hpp>

#include <filesystem>
#include <regex>

namespace fs = std::filesystem;
using namespace std::literals;

TEST_CASE("Load zip", "[resources]")
{
    nw::Zip z{"test_data/temp0.zip"};
    REQUIRE(z.valid());
    REQUIRE(z.extract(std::regex(".*"), "tmp") == 23);

    nw::Zip z2{"test_data/non-extant.zip"};
    REQUIRE(!z2.valid());
}
