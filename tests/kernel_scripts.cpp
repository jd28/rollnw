#ifdef ROLLNW_BUILD_RUNTIME_SCRIPTING

#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/ScriptSystem.hpp>

namespace nwk = nw::kernel;

TEST_CASE("scripts: basic luau test", "[kernel]")
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    REQUIRE(mod);

    auto L = nwk::scripts().global_state();
    REQUIRE(L);
    lua_getfield(L, LUA_GLOBALSINDEX, "math");
    REQUIRE(lua_istable(L, -1));

    nw::kernel::unload_module();
}

#endif // ROLLNW_BUILD_RUNTIME_SCRIPTING
