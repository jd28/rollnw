#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/resources/Key.hpp>

#include <filesystem>
#include <regex>

namespace fs = std::filesystem;
using namespace std::literals;

TEST(Key, Construction)
{
    auto install_path = nw::kernel::config().options().install;
    if (fs::exists(install_path / "data/nwn_base.key")) {
        nw::Key k{install_path / "data/nwn_base.key"};
        EXPECT_GT(k.size(), 0);
        EXPECT_GT(k.all().size(), 0);

        nw::ResourceData data = k.demand({"nwscript"sv, nw::ResourceType::nss});
        EXPECT_TRUE(data.bytes.size());
        EXPECT_EQ(k.extract(std::regex("nwscript\\.nss"), "tmp/"), 1);
    }
}

TEST(Key, visit)
{

    auto install_path = nw::kernel::config().options().install;
    if (fs::exists(install_path / "data/nwn_base.key")) {
        nw::Key k{install_path / "data/nwn_base.key"};

        size_t count = 0;
        auto visitor = [&count](const nw::Resource&) {
            ++count;
        };
        k.visit(visitor);
        EXPECT_EQ(k.size(), count);
    }
}
