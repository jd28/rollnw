#include <catch2/catch_all.hpp>

#include <nw/util/string.hpp>

using namespace std::literals;

TEST_CASE("String split", "[utils]")
{
    auto v = nw::string::split("a,b", ',');
    REQUIRE(v[0] == "a");
    REQUIRE(v[1] == "b");
}

TEST_CASE("String join", "[utils]")
{
    std::vector<std::string> v{"a", "b", "c"};
    std::string s = nw::string::join(v, "::");
    REQUIRE(s == "a::b::c");
}

TEST_CASE("String conversions", "[utils]")
{
    auto o1 = nw::string::from<double>("1.0");
    REQUIRE(o1);
    REQUIRE(*o1 == 1.0);

    o1 = nw::string::from<double>("abcdf");
    REQUIRE(!o1);

    auto o2 = nw::string::from<int32_t>("1");
    REQUIRE(o2);
    REQUIRE(*o2 == 1);

    auto o3 = nw::string::from<bool>("1");
    REQUIRE(o3);
    REQUIRE(*o3 == true);
}

TEST_CASE("String trim", "[utils]")
{
    std::string s{"   a"};
    nw::string::ltrim_in_place(&s);
    REQUIRE(s == "a");
    s = "a   ";
    nw::string::rtrim_in_place(&s);
    REQUIRE(s == "a");
    s = "   a   ";
    nw::string::trim_in_place(&s);
    REQUIRE(s == "a");
}

TEST_CASE("String comparisons", "[utils]")
{
    REQUIRE(nw::string::endswith("test.dds"sv, ".dds"));
    REQUIRE(nw::string::endswith("test.dds", ".dds"s));
    REQUIRE(nw::string::endswith("test.dds"s, ".dds"sv));
    REQUIRE(nw::string::endswith("test.dds"sv, ".dds"s));

    REQUIRE(nw::string::startswith("test.dds"sv, "test"));
    REQUIRE(nw::string::startswith("test.dds", "test"s));
    REQUIRE(nw::string::startswith("test.dds"s, "test"sv));
    REQUIRE(nw::string::startswith("test.dds"sv, "test"s));
}

TEST_CASE("Glob to regex", "[utils]")
{
    auto match = [](std::string_view glob, const char* target) -> bool {
        auto re = nw::string::glob_to_regex(glob);
        return std::regex_match(target, re);
    };

    REQUIRE(match("nwscript.nss", "nwscript.nss") == true);
    REQUIRE(match("a", "b") == false);
    REQUIRE(match("test.ut[cmti]*"s, "test.utc") == true);
    REQUIRE(match("a?", "a") == false);
    REQUIRE(match("a?", "ab") == true);
    REQUIRE(match("a*b", "ab") == true);
    REQUIRE(match("a*b", "acb") == true);
    REQUIRE(match("a*b", "abc") == false);
    REQUIRE(match("*a*??????a?????????a???????????????",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

TEST_CASE("Covert colors", "[utils]")
{
    std::string test{"<c\x01\x11\xFE>"};
    auto s = nw::string::sanitize_colors(test);
    REQUIRE(s == "<c0111FE>");
    auto ds = nw::string::desanitize_colors("<c0111FE>");
    REQUIRE(ds == test);

    std::string test2{"<cab>"};
    auto s2 = nw::string::sanitize_colors(test2);
    REQUIRE(s2 == "<cab>");

    std::string test3{"<cabc>test</c>"};
    auto s3 = nw::string::sanitize_colors(test3);
    REQUIRE(s3 == "<c616263>test</c>");
    auto ds2 = nw::string::desanitize_colors("<c616263>test</c>");
    REQUIRE(ds2 == test3);
}
