
#ifdef ROLLNW_ENABLE_LEGACY

#include <catch2/catch_all.hpp>

#include <nw/legacy/Plt.hpp>
#include <nw/log.hpp>

#include <filesystem>

TEST_CASE("Parse plt", "[formats]")
{
    nw::Plt plt{"test_data/user/development/pmh0_head001.plt"};
    REQUIRE(plt.valid());
    REQUIRE(plt.height() == 256);
    REQUIRE(plt.width() == 256);
}

#endif // ROLLNW_ENABLE_LEGACY
