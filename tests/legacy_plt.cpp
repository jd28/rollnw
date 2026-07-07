

#include <gtest/gtest.h>

#include <nw/formats/Plt.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <cstring>
#include <filesystem>
#include <limits>

namespace {

nw::ResourceData make_plt(uint32_t width, uint32_t height, std::initializer_list<nw::PltPixel> pixels)
{
    nw::ResourceData data;
    data.bytes.resize(24 + pixels.size() * sizeof(nw::PltPixel));
    std::memcpy(data.bytes.data(), "PLT V1  ", 8);
    std::memcpy(data.bytes.data() + 16, &width, sizeof(width));
    std::memcpy(data.bytes.data() + 20, &height, sizeof(height));
    if (pixels.size() != 0) {
        std::memcpy(data.bytes.data() + 24, pixels.begin(), pixels.size() * sizeof(nw::PltPixel));
    }
    return data;
}

} // namespace

TEST(Plt, Load)
{
    nw::Plt plt{"test_data/user/development/pmh0_head001.plt"};
    EXPECT_TRUE(plt.valid());
    EXPECT_EQ(plt.height(), 256u);
    EXPECT_EQ(plt.width(), 256u);

    EXPECT_TRUE(nw::kernel::resman().palette_texture(nw::plt_layer_skin));
    auto value = plt.pixels()[0];
    EXPECT_EQ(value.layer, nw::PltLayer::plt_layer_skin);
    EXPECT_EQ(value.color, 62);
    auto color = nw::decode_plt_color(plt, {0}, 0, 0);
    EXPECT_EQ(color, 0xFF51668B);
}

TEST(Plt, ToImage)
{
    nw::Plt plt{"test_data/user/development/pmh0_head001.plt"};
    EXPECT_TRUE(plt.valid());
    nw::PltColors colors;
    colors.data[uint8_t(nw::PltLayer::plt_layer_hair)] = 10;
    colors.data[uint8_t(nw::PltLayer::plt_layer_skin)] = 56;

    nw::Image img(plt, colors);
    EXPECT_TRUE(img.valid());
    EXPECT_TRUE(img.write_to("tmp/pmh0_head001.png"));
}

TEST(Plt, RejectsOverflowDimensions)
{
    nw::Plt plt{make_plt(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), {})};
    EXPECT_FALSE(plt.valid());
    EXPECT_EQ(nw::decode_plt_color(plt, {}, 0, 0), 0u);
}

TEST(Plt, DecodeRejectsInvalidLayer)
{
    nw::Plt plt{make_plt(1, 1, {nw::PltPixel{1, nw::plt_layer_size}})};
    ASSERT_TRUE(plt.valid());
    EXPECT_EQ(nw::decode_plt_color(plt, {}, 0, 0), 0u);
}
