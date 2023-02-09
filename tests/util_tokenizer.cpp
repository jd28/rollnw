#include <gtest/gtest.h>

#include <nw/util/Tokenizer.hpp>

using namespace std::literals;

TEST(Tokenizer, MtrStyle)
{
    auto test = R"x(
        mtr style
        // Skinned
        customshaderVS vslit_sk_nm)x"sv;

    nw::Tokenizer t{test, "//"sv};
    EXPECT_EQ(t.next(), "mtr");
    EXPECT_EQ(t.next(), "style");
    EXPECT_EQ(t.next(), "customshaderVS");
    EXPECT_EQ(t.next(), "vslit_sk_nm");
}

TEST(Tokenizer, TxiStyle)
{
    auto test = R"x(
        # Default is set to 1.
        bumpmapscaling 1.5)x"sv;

    nw::Tokenizer t{test, "#"sv};
    EXPECT_EQ(t.next(), "bumpmapscaling");
    EXPECT_EQ(t.next(), "1.5");
}
