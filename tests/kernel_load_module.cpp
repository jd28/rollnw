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
    nw::Area& area = *mod->get_area(0);
    REQUIRE(area.common()->resref == "test_area");
    REQUIRE(area.creatures.size() > 0);
    REQUIRE(area.creatures[0]->hp_max == 110);

    nw::kernel::unload_module();
}
