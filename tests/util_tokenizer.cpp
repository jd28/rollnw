#include <catch2/catch_all.hpp>

#include <nw/util/Tokenizer.hpp>

using namespace std::literals;

TEST_CASE("Tokenizer mtr style", "[utils]")
{
    auto test = R"x(
        mtr style
        // Skinned
        customshaderVS vslit_sk_nm)x"sv;

    nw::Tokenizer t{test, "//"sv};
    REQUIRE(t.next() == "mtr");
    REQUIRE(t.next() == "style");
    REQUIRE(t.next() == "customshaderVS");
    REQUIRE(t.next() == "vslit_sk_nm");
}

TEST_CASE("Tokenizer texi style", "[utils]")
{
    auto test = R"x(
        # Default is set to 1.
        bumpmapscaling 1.5)x"sv;

    nw::Tokenizer t{test, "#"sv};
    REQUIRE(t.next() == "bumpmapscaling");
    REQUIRE(t.next() == "1.5");
}
