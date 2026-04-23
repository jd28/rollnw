#include <gtest/gtest.h>

#include <nw/formats/Txi.hpp>

using namespace std::literals;

namespace {

nw::ResourceData make_txi(std::string_view text)
{
    nw::ResourceData result;
    result.bytes.append(text.data(), text.size());
    return result;
}

}

TEST(Txi, ParsesBasicValues)
{
    nw::Txi txi{make_txi("blending punchthrough\nalphamean 0.7\n")};
    EXPECT_TRUE(txi.valid());

    nw::String blending;
    EXPECT_TRUE(txi.get_to("blending", blending));
    EXPECT_EQ(blending, "punchthrough"s);

    float alphamean = 0.0f;
    EXPECT_TRUE(txi.get_to("alphamean", alphamean));
    EXPECT_FLOAT_EQ(alphamean, 0.7f);
}

TEST(Txi, IgnoresCommentsAndIsCaseInsensitive)
{
    nw::Txi txi{make_txi("# comment\nBlendING Additive # trailing\n")};
    EXPECT_TRUE(txi.valid());

    nw::String blending;
    EXPECT_TRUE(txi.get_to("BLENDING", blending));
    EXPECT_EQ(blending, "Additive"s);
}

TEST(Txi, LastValueWins)
{
    nw::Txi txi{make_txi("blending normal\nblending punchthrough\n")};
    EXPECT_TRUE(txi.valid());

    nw::String blending;
    EXPECT_TRUE(txi.get_to("blending", blending));
    EXPECT_EQ(blending, "punchthrough"s);
}

TEST(Txi, SupportsQuotedValues)
{
    nw::Txi txi{make_txi("defaultwidth \"32\"\n")};
    EXPECT_TRUE(txi.valid());

    int value = 0;
    EXPECT_TRUE(txi.get_to("defaultwidth", value));
    EXPECT_EQ(value, 32);
}
