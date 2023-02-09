#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ParsedScriptCache.hpp>

namespace nwk = nw::kernel;

TEST(KernelParsedScriptCache, Get)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto s1 = nwk::parsed_scripts().get("nwscript");
    EXPECT_TRUE(s1);
    auto s2 = nwk::parsed_scripts().get("nwscript");
    EXPECT_EQ(s1, s2);
    auto s3 = nwk::parsed_scripts().get("dontexist");
    EXPECT_TRUE(!s3);

    nwk::unload_module();
}
