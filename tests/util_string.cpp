#include <gtest/gtest.h>

#include <nw/util/string.hpp>

using namespace std::literals;

TEST(String, split)
{
    auto v = nw::string::split("a,b", ',');
    EXPECT_EQ(v[0], "a");
    EXPECT_EQ(v[1], "b");
}

TEST(String, join)
{
    std::vector<std::string> v{"a", "b", "c"};
    std::string s = nw::string::join(v, "::");
    EXPECT_EQ(s, "a::b::c");
}

TEST(String, NumericConversion)
{
    auto o1 = nw::string::from<double>("1.0");
    EXPECT_TRUE(o1);
    EXPECT_EQ(*o1, 1.0);

    o1 = nw::string::from<double>("abcdf");
    EXPECT_TRUE(!o1);

    auto o2 = nw::string::from<int32_t>("1");
    EXPECT_TRUE(o2);
    EXPECT_EQ(*o2, 1);

    auto o3 = nw::string::from<bool>("1");
    EXPECT_TRUE(o3);
    EXPECT_EQ(*o3, true);
}

TEST(String, trim)
{
    std::string s{"   a"};
    nw::string::ltrim_in_place(&s);
    EXPECT_EQ(s, "a");
    s = "a   ";
    nw::string::rtrim_in_place(&s);
    EXPECT_EQ(s, "a");
    s = "   a   ";
    nw::string::trim_in_place(&s);
    EXPECT_EQ(s, "a");
}

TEST(String, Comparisons)
{
    EXPECT_TRUE(nw::string::endswith("test.dds"sv, ".dds"));
    EXPECT_TRUE(nw::string::endswith("test.dds", ".dds"s));
    EXPECT_TRUE(nw::string::endswith("test.dds"s, ".dds"sv));
    EXPECT_TRUE(nw::string::endswith("test.dds"sv, ".dds"s));

    EXPECT_TRUE(nw::string::startswith("test.dds"sv, "test"));
    EXPECT_TRUE(nw::string::startswith("test.dds", "test"s));
    EXPECT_TRUE(nw::string::startswith("test.dds"s, "test"sv));
    EXPECT_TRUE(nw::string::startswith("test.dds"sv, "test"s));
}

TEST(String, Glob2Regex)
{
    auto match = [](std::string_view glob, const char* target) -> bool {
        auto re = nw::string::glob_to_regex(glob);
        return std::regex_match(target, re);
    };

    EXPECT_EQ(match("nwscript.nss", "nwscript.nss"), true);
    EXPECT_EQ(match("a", "b"), false);
    EXPECT_EQ(match("test.ut[cmti]*"s, "test.utc"), true);
    EXPECT_EQ(match("a?", "a"), false);
    EXPECT_EQ(match("a?", "ab"), true);
    EXPECT_EQ(match("a*b", "ab"), true);
    EXPECT_EQ(match("a*b", "acb"), true);
    EXPECT_EQ(match("a*b", "abc"), false);
    EXPECT_TRUE(match("*a*??????a?????????a???????????????",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

TEST(String, ColorConversion)
{
    std::string test{"<c\x01\x11\xFE>"};
    auto s = nw::string::sanitize_colors(test);
    EXPECT_EQ(s, "<c0111FE>");
    auto ds = nw::string::desanitize_colors("<c0111FE>");
    EXPECT_EQ(ds, test);

    std::string test2{"<cab>"};
    auto s2 = nw::string::sanitize_colors(test2);
    EXPECT_EQ(s2, "<cab>");

    std::string test3{"<cabc>test</c>"};
    auto s3 = nw::string::sanitize_colors(test3);
    EXPECT_EQ(s3, "<c616263>test</c>");
    auto ds2 = nw::string::desanitize_colors("<c616263>test</c>");
    EXPECT_EQ(ds2, test3);
}
