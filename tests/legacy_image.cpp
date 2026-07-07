

#include <gtest/gtest.h>

#include <nw/formats/Image.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <cstring>
#include <filesystem>

#include <nowide/cstdlib.hpp>

using namespace std::literals;

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
    auto bytes = dds2.release();
    EXPECT_FALSE(dds2.valid());
    if (bytes) { free(bytes); }
}

TEST(Image, RejectsOversizedBiowareDDS)
{
    struct BiowareDdsHeader {
        uint32_t width;
        uint32_t height;
        uint32_t colors;
        uint32_t reserved[2];
    };

    BiowareDdsHeader header{};
    header.width = 16385;
    header.height = 16384;
    header.colors = 4;

    nw::ResourceData data;
    data.name = nw::Resource{"oversized"sv, nw::ResourceType::dds};
    data.bytes.append(&header, sizeof(header));

    nw::Image image{std::move(data), false};
    EXPECT_FALSE(image.valid());
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
    EXPECT_EQ(tga.width(), 128u);
    EXPECT_EQ(tga.height(), 256u);
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.dds"));
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.png"));
    EXPECT_TRUE(tga.write_to("tmp/qfpp_001_L.tga"));

    nw::Image generated_dds{"tmp/qfpp_001_L.dds"};
    EXPECT_TRUE(generated_dds.valid());
    EXPECT_FALSE(generated_dds.is_bio_dds());
    EXPECT_EQ(generated_dds.width(), 128u);
    EXPECT_EQ(generated_dds.height(), 256u);
}

TEST(Image, BMP)
{
    // if (!nowide::getenv("CI_GITHUB_ACTIONS")) {
    //     for (auto resref : {"mvpal_skin"sv, "mvpal_hair"sv, "mvpal_cloth"sv, "mvpal_leather"sv, "mvpal_tattoo"sv, "mvpal_armor01"sv}) {
    //         auto data = nw::kernel::resman().demand({resref, nw::ResourceType::bmp});
    //         nw::Image bmp{std::move(data)};
    //         EXPECT_TRUE(bmp.valid());
    //         EXPECT_TRUE(bmp.write_to(fmt::format("tmp/{}.png", resref)));
    //     }
    // }
}
