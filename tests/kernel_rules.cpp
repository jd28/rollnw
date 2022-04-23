#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/util/game_install.hpp>

TEST_CASE("rules manager", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::kernel::unload_module();
}
