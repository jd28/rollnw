#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>

using namespace std::literals;

TEST_CASE("load module", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);
    REQUIRE(mod->area_count() == 1);
    REQUIRE((*mod)[0]->common()->resref == "test_area");
    nw::kernel::unload_module();
}
