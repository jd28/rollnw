#ifdef ROLLNW_BUILD_RUNTIME_SCRIPTING

#include "ScriptSystem.hpp"

namespace nw::kernel {

void ScriptSystem::initialize()
{
    if (!global_) {
        global_ = StateRef(luaL_newstate(), lua_close);
        luaL_openlibs(global_.get());
    }
}

lua_State* ScriptSystem::global_state()
{
    return global_.get();
}

} // namespace nw::kernel

#endif // ROLLNW_BUILD_RUNTIME_SCRIPTING
