
#ifdef ROLLNW_ENABLE_LEGACY

#include <gtest/gtest.h>

#include <nw/kernel/Resources.hpp>
#include <nw/legacy/Plt.hpp>
#include <nw/log.hpp>

#include <filesystem>

TEST(Plt, Load)
{
    nw::Plt plt{"test_data/user/development/pmh0_head001.plt"};
    EXPECT_TRUE(plt.valid());
    EXPECT_EQ(plt.height(), 256);
    EXPECT_EQ(plt.width(), 256);

    EXPECT_TRUE(nw::kernel::resman().palette_texture(nw::plt_layer_skin));
    auto color = nw::decode_plt_color(plt, {0}, 0, 0);
    EXPECT_EQ(size_t(color[0]), 139);
    EXPECT_EQ(size_t(color[1]), 102);
    EXPECT_EQ(size_t(color[2]), 81);
}

#endif // ROLLNW_ENABLE_LEGACY
