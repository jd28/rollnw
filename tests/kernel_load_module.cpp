#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/profiles/nwn1/Profile.hpp>

#include <nowide/cstdlib.hpp>

using namespace std::literals;

TEST_CASE("load real module", "[kernel]")
{
    std::string mod_path;
    if (const char* var = nowide::getenv("ROLLNW_TEST_MODULE")) {
        mod_path = var;
    }
    if (!mod_path.empty()) {
        auto mod = nw::kernel::load_module(mod_path);
        REQUIRE(mod.is_alive());
        REQUIRE(mod.get<nw::Module>()->area_count());
        nw::kernel::unload_module();
    }
}

TEST_CASE("load module from .mod", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod.is_alive());
    REQUIRE(mod.get<nw::Module>()->area_count() == 1);
    auto area = mod.get<nw::Module>()->get_area(0);
    REQUIRE(area.is_alive());
    REQUIRE(area.get<nw::Common>()->resref == "start");
    nw::kernel::unload_module();
}

TEST_CASE("load module from directory", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_dir/");
    REQUIRE(mod.is_alive());
    REQUIRE(mod.get<nw::Module>()->area_count() == 1);
    auto area = mod.get<nw::Module>()->get_area(0);
    REQUIRE(area.get<nw::Common>()->resref == "test_area");
    REQUIRE(area.get<nw::Area>()->creatures.size() > 0);
    REQUIRE(area.get<nw::Area>()->creatures[0].get<nw::Creature>()->hp_max == 110);
    auto cre = nw::kernel::objects().load("test_creature", nw::ObjectType::creature);
    REQUIRE(cre.is_alive());

    nw::kernel::unload_module();
}

TEST_CASE("load module from .zip", "[kernel]")
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_zip.zip");
    REQUIRE(mod.is_alive());
    REQUIRE(mod.get<nw::Module>()->area_count() == 1);
    auto area = mod.get<nw::Module>()->get_area(0);
    REQUIRE(area.get<nw::Common>()->resref == "test_area");
    REQUIRE(area.get<nw::Area>()->creatures.size() > 0);
    REQUIRE(area.get<nw::Area>()->creatures[0].get<nw::Creature>()->hp_max == 110);
    auto cre = nw::kernel::objects().load("test_creature", nw::ObjectType::creature);
    REQUIRE(cre.is_alive());

    nw::kernel::unload_module();
}
