#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/rules/system.hpp>

TEST_CASE("baseitems", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::kernel::unload_module();
}
