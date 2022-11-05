#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TwoDACache.hpp>

namespace nwk = nw::kernel;

TEST_CASE("twoda cache", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto s1 = nwk::twodas().get("placeables");
    REQUIRE(s1);
    auto s2 = nwk::twodas().get("placeables");
    REQUIRE(s1 == s2);
    auto s3 = nwk::twodas().get("dontexist");
    REQUIRE(!s3);

    nwk::unload_module();
}
