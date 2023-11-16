#include "nw/kernel/Kernel.hpp"
#include <gtest/gtest.h>

#include <nw/kernel/Resources.hpp>
#include <nw/log.hpp>
#include <nw/resources/Erf.hpp>
#include <nw/resources/NWSync.hpp>
#include <nw/resources/Zip.hpp>

using namespace std::literals;
namespace nwk = nw::kernel;

TEST(KernelResources, AddContainer)
{
    auto rm = new nw::kernel::Resources;
    auto sz = rm->size();
    nw::Erf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(rm->add_container(&e, false));
    EXPECT_TRUE(rm->contains({"module"sv, nw::ResourceType::ifo}));
    EXPECT_EQ(rm->size(), sz + e.size());

    nw::kernel::Resources rm2{rm};
    EXPECT_TRUE(rm2.contains({"module"sv, nw::ResourceType::ifo}));

    delete rm;
}

TEST(KernelResources, Extract)
{
    auto rm = new nw::kernel::Resources;
    EXPECT_TRUE(rm->add_container(new nw::Erf("test_data/user/modules/DockerDemo.mod")));
    EXPECT_TRUE(rm->add_container(new nw::Zip("test_data/user/modules/module_as_zip.zip")));
    EXPECT_FALSE(rm->add_container(new nw::Zip("test_data/user/modules/module_as_zip.zip")));
    EXPECT_TRUE(rm->contains({"module"sv, nw::ResourceType::ifo}));
    EXPECT_TRUE(rm->contains({"test_area"sv, nw::ResourceType::are}));
    EXPECT_EQ(rm->extract(std::regex(".*"), "tmp"), 37);
    rm->clear_containers();
    EXPECT_FALSE(rm->contains({"test_area"sv, nw::ResourceType::are}));

    delete rm;
}

TEST(KernelResources, LoadModule)
{
    auto rm = new nw::kernel::Resources;
    auto path = nw::kernel::config().alias_path(nw::PathAlias::nwsync);
    auto n = nw::NWSync(path);
    EXPECT_TRUE(n.is_loaded());
    auto manifests = n.manifests();

    if (manifests.size() > 0) {
        EXPECT_TRUE(rm->load_module("test_data/user/modules/DockerDemo.mod", manifests[0]));
        rm->unload_module();
    } else {
        EXPECT_TRUE(rm->load_module("test_data/user/modules/DockerDemo.mod"));
        rm->unload_module();
    }
    delete rm;
}

TEST(KernelResources, LoadPlayerCharacter)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto data = nwk::resman().demand_server_vault("CDKEY", "testsorcpc1");
    EXPECT_TRUE(data.bytes.size());

    data = nwk::resman().demand_server_vault("WRONGKEY", "testsorcpc1");
    EXPECT_FALSE(data.bytes.size());

    data = nwk::resman().demand_server_vault("CDKEY", "WRONGNAME");
    EXPECT_FALSE(data.bytes.size());

    nwk::unload_module();
}

TEST(KernelResources, visit)
{
    auto rm = new nw::kernel::Resources;
    auto sz = rm->size();
    nw::Erf e("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(rm->add_container(&e, false));
    EXPECT_TRUE(rm->contains({"module"sv, nw::ResourceType::ifo}));

    size_t count = 0;
    auto visitor = [&count](const nw::Resource&) {
        ++count;
    };

    rm->visit(visitor);
    EXPECT_EQ(rm->size(), sz + e.size());
    delete rm;
}
