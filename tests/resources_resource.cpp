#include <catch2/catch_all.hpp>

#include <nw/log.hpp>
#include <nw/resources/Resource.hpp>

using namespace nw;
using namespace std::literals;

TEST_CASE("resource::from_path", "[resources]")
{
    std::filesystem::path p1{"test.utc"};
    auto r1 = Resource::from_path(p1);
    REQUIRE(r1.valid());

    std::filesystem::path p2{"test.xxx"};
    auto r2 = Resource::from_path(p2);
    REQUIRE_FALSE(r2.valid());

    std::filesystem::path p3{".xxx"};
    auto r3 = Resource::from_path(p3);
    REQUIRE_FALSE(r3.valid());

    std::filesystem::path p4{""};
    auto r4 = Resource::from_path(p4);
    REQUIRE_FALSE(r4.valid());

    std::filesystem::path p5{"test/test.ini"};
    auto r5 = Resource::from_path(p5);
    REQUIRE(r5.valid());

    std::filesystem::path p6{"test/test_this_is_too_long_for_resref.ini"};
    auto r6 = Resource::from_path(p6);
    REQUIRE_FALSE(r6.valid());
}

TEST_CASE("resource::from_filename", "[resources]")
{
    std::string p1{"test.utc"};
    auto r1 = Resource::from_filename(p1);
    REQUIRE(r1.valid());

    std::string p2{"test.xxx"};
    auto r2 = Resource::from_filename(p2);
    REQUIRE_FALSE(r2.valid());

    std::string p3{".xxx"};
    auto r3 = Resource::from_filename(p3);
    REQUIRE_FALSE(r3.valid());

    std::string p4{""};
    auto r4 = Resource::from_filename(p4);
    REQUIRE_FALSE(r4.valid());

    std::string p6{"test_this_is_too_long_for_resref.ini"};
    auto r6 = Resource::from_filename(p6);
    REQUIRE_FALSE(r6.valid());
}
