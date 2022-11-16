#include <catch2/catch_all.hpp>

#include <nw/components/Area.hpp>
#include <nw/components/Module.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/log.hpp>
#include <nwn1/Profile.hpp>

#include <nowide/cstdlib.hpp>

using namespace std::literals;

TEST_CASE("load module from .mod", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);
    REQUIRE(mod->area_count() == 1);
    auto area = mod->get_area(0);
    REQUIRE(area);
    REQUIRE(area->common.resref == "start");
    nw::kernel::unload_module();
}

TEST_CASE("load module from directory", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_dir/");
    REQUIRE(mod);
    REQUIRE(mod->area_count() == 1);
    auto area = mod->get_area(0);
    REQUIRE(area->common.resref == "test_area");
    REQUIRE(area->creatures.size() > 0);
    REQUIRE(area->creatures[0]->hp_max == 110);
    auto cre = nw::kernel::objects().load<nw::Creature>("test_creature"sv);
    REQUIRE(cre);

    nw::kernel::unload_module();
}

TEST_CASE("load module from .zip", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_zip.zip");
    REQUIRE(mod);
    REQUIRE(mod->area_count() == 1);
    auto area = mod->get_area(0);
    REQUIRE(area->common.resref == "test_area");
    REQUIRE(area->creatures.size() > 0);
    REQUIRE(area->creatures[0]->hp_max == 110);
    auto cre = nw::kernel::objects().load<nw::Creature>("test_creature"sv);
    REQUIRE(cre);

    nw::kernel::unload_module();
}
