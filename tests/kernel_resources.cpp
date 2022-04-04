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
}

TEST_CASE("resources: load module", "[kernel]")
{
    auto& rm = nw::kernel::resman();
    auto user_path = nw::kernel::config().options().user;
    auto n = nw::NWSync(user_path / "nwsync/");
    REQUIRE(n.is_loaded());
    auto manifests = n.manifests();

    nw::Module* mod = nullptr;
    if (manifests.size() > 0) {
        mod = rm.load_module("DockerDemo", manifests[0]);
    } else {
        mod = rm.load_module("DockerDemo");
    }
    REQUIRE(mod);
}
