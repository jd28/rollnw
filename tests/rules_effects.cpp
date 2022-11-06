#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/rules/Effect.hpp>

TEST_CASE("effects", "[rules]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    nw::Effect eff;
    eff.set_string(12, "my string");
    REQUIRE(eff.get_string(12) == "my string");
    REQUIRE(eff.get_int(3) == 0);
    
    nw::kernel::unload_module();
}
