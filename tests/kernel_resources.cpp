#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/NWSync.hpp>
#include <nw/resources/Zip.hpp>

using namespace std::literals;

TEST_CASE("resources", "[kernel]")
{
    auto rm = new nw::kernel::Resources;
    auto sz = rm->size();
    nw::Erf e("test_data/user/modules/DockerDemo.mod");
    REQUIRE(rm->add_container(&e, false));
    REQUIRE(rm->contains({"module"sv, nw::ResourceType::ifo}));
    REQUIRE(rm->size() == sz + e.size());
}

TEST_CASE("resources: extract", "[kernel]")
{
    auto rm = new nw::kernel::Resources;
    REQUIRE(rm->add_container(new nw::Erf("test_data/user/modules/DockerDemo.mod")));
    REQUIRE(rm->add_container(new nw::Zip("test_data/user/modules/module_as_zip.zip")));
    REQUIRE_FALSE(rm->add_container(new nw::Zip("test_data/user/modules/module_as_zip.zip")));
    REQUIRE(rm->contains({"module"sv, nw::ResourceType::ifo}));
    REQUIRE(rm->contains({"test_area"sv, nw::ResourceType::are}));
    REQUIRE(rm->extract(std::regex(".*"), "tmp") == 37);
    rm->clear_containers();
    REQUIRE_FALSE(rm->contains({"test_area"sv, nw::ResourceType::are}));
}

TEST_CASE("resources: load module", "[kernel]")
{
    auto rm = new nw::kernel::Resources;
    auto path = nw::kernel::config().alias_path(nw::PathAlias::nwsync);
    auto n = nw::NWSync(path);
    REQUIRE(n.is_loaded());
    auto manifests = n.manifests();

    if (manifests.size() > 0) {
        REQUIRE(rm->load_module("test_data/user/modules/DockerDemo.mod", manifests[0]));
        rm->unload_module();
    } else {
        REQUIRE(rm->load_module("test_data/user/modules/DockerDemo.mod"));
        rm->unload_module();
    }
}
