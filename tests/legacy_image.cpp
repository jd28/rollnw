#ifdef ROLLNW_ENABLE_LEGACY

#include <gtest/gtest.h>

#include <nw/legacy/Image.hpp>
#include <nw/log.hpp>

#include <filesystem>

TEST(Image, BiowareDDS)
{
    nw::Image dds{"test_data/user/development/bioRGBA.dds"};
    EXPECT_TRUE(dds.valid());
#ifndef _WIN32
    EXPECT_TRUE(dds.write_to("tmp/bioRGBA.dds"));
    EXPECT_TRUE(dds.write_to("tmp/bioRGBA.png"));
    EXPECT_TRUE(dds.write_to("tmp/bioRGBA.tga"));
#endif

    nw::Image dds2{"test_data/user/development/tno01_wtcliff01.dds"};
    EXPECT_TRUE(dds2.valid());
    EXPECT_EQ(dds2.channels(), 3u);
#ifndef _WIN32
    EXPECT_TRUE(dds2.write_to("tmp/tno01_wtcliff01.dds"));
    EXPECT_TRUE(dds2.write_to("tmp/tno01_wtcliff01.png"));
    EXPECT_TRUE(dds2.write_to("tmp/tno01_wtcliff01.tga"));
#endif
}

TEST(Image, StandardDDS)
{
    nw::Image dds{"test_data/user/development/dxtRBG.dds"};
    EXPECT_TRUE(dds.valid());
#ifndef _WIN32
    EXPECT_TRUE(dds.write_to("tmp/dxtRBG.dds"));
    EXPECT_TRUE(dds.write_to("tmp/dxtRBG.png"));
    EXPECT_TRUE(dds.write_to("tmp/dxtRBG.tga"));
#endif
}

TEST(Image, TGA)
{
    nw::Image tga{"test_data/user/development/qfpp_001_L.tga"};
    EXPECT_TRUE(tga.valid());
#ifndef _WIN32
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.dds"));
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.png"));
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.tga"));
#endif
}

#endif // #ifdef ROLLNW_ENABLE_LEGACY
