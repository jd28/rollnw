

#include <gtest/gtest.h>

#include <nw/formats/Image.hpp>
#include <nw/log.hpp>

#include <filesystem>

TEST(Image, BiowareDDS)
{
    nw::Image dds{"test_data/user/development/bioRGBA.dds"};
    EXPECT_TRUE(dds.valid());
    EXPECT_TRUE(dds.is_bio_dds());
    EXPECT_TRUE(dds.write_to("tmp/bioRGBA.dds"));
    EXPECT_TRUE(dds.write_to("tmp/bioRGBA.png"));
    EXPECT_TRUE(dds.write_to("tmp/bioRGBA.tga"));

    nw::Image dds2{"test_data/user/development/tno01_wtcliff01.dds"};
    EXPECT_TRUE(dds2.valid());
    EXPECT_EQ(dds2.channels(), 3u);
    EXPECT_TRUE(dds.is_bio_dds());
    EXPECT_TRUE(dds2.write_to("tmp/tno01_wtcliff01.dds"));
    EXPECT_TRUE(dds2.write_to("tmp/tno01_wtcliff01.png"));
    EXPECT_TRUE(dds2.write_to("tmp/tno01_wtcliff01.tga"));
}

TEST(Image, StandardDDS)
{
    nw::Image dds{"test_data/user/development/dxtRBG.dds"};
    EXPECT_TRUE(dds.valid());
    EXPECT_FALSE(dds.is_bio_dds());
    EXPECT_TRUE(dds.write_to("tmp/dxtRBG.dds"));
    EXPECT_TRUE(dds.write_to("tmp/dxtRBG.png"));
    EXPECT_TRUE(dds.write_to("tmp/dxtRBG.tga"));
}

TEST(Image, TGA)
{
    nw::Image tga{"test_data/user/development/qfpp_001_L.tga"};
    EXPECT_TRUE(tga.valid());
    EXPECT_FALSE(tga.is_bio_dds());
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.dds"));
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.png"));
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.tga"));
}
