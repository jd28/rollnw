#pragma once

#ifdef ROLLNW_BUILD_RUNTIME_SCRIPTING

#include "../log.hpp"
#include "Kernel.hpp"

#include <luau/VM/include/lua.h>
#include <luau/VM/include/luaconf.h>
#include <luau/VM/include/lualib.h>

namespace nw::kernel {

using StateRef = std::unique_ptr<lua_State, void (*)(lua_State*)>;

struct ScriptSystem : public Service {
    ScriptSystem() = default;

    virtual void initialize() override;
    virtual void clear() override { }

    /// Gets global Luau state
    lua_State* global_state();

private:
    StateRef global_{nullptr, nullptr};
};

inline ScriptSystem& scripts()
{
    auto res = detail::s_services.scripts.get();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load scripts service");
    }
    return *res;
}
} // namespace nw::kernel

#endif // ROLLNW_BUILD_RUNTIME_SCRIPTING
