#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/rules/system.hpp>

TEST(Rules, Baseitems)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    nw::kernel::unload_module();
}
