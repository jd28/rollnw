

#include <gtest/gtest.h>

#include <nw/formats/Plt.hpp>
#include <nw/kernel/Resources.hpp>
#include <nw/log.hpp>

#include <filesystem>

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
    EXPECT_EQ(color, 0x8B6651FF);
}
