
#ifdef ROLLNW_ENABLE_LEGACY

#include <catch2/catch_all.hpp>

#include <nw/kernel/Resources.hpp>
#include <nw/legacy/Plt.hpp>
#include <nw/log.hpp>

#include <filesystem>

TEST_CASE("Parse plt", "[formats]")
{
    nw::Plt plt{"test_data/user/development/pmh0_head001.plt"};
    REQUIRE(plt.valid());
    REQUIRE(plt.height() == 256);
    REQUIRE(plt.width() == 256);

    REQUIRE(nw::kernel::resman().palette_texture(nw::plt_layer_skin));
    auto color = nw::decode_plt_color(plt, {0}, 0, 0);
    REQUIRE(size_t(color[0]) == 139);
    REQUIRE(size_t(color[1]) == 102);
    REQUIRE(size_t(color[2]) == 81);
}

#endif // ROLLNW_ENABLE_LEGACY
