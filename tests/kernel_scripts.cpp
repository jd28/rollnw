#ifdef ROLLNW_BUILD_RUNTIME_SCRIPTING

#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ScriptSystem.hpp>

namespace nwk = nw::kernel;

TEST(ScriptSystem, GlobalAccess)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto L = nwk::scripts().global_state();
    EXPECT_TRUE(L);
    lua_getfield(L, LUA_GLOBALSINDEX, "math");
    EXPECT_TRUE(lua_istable(L, -1));

    nw::kernel::unload_module();
}

#endif // ROLLNW_BUILD_RUNTIME_SCRIPTING
