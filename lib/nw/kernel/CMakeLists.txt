add_library(nw-kernel STATIC
    Kernel.cpp
    Config.cpp
    EffectSystem.cpp
    EventSystem.cpp
    Objects.cpp
    Resources.cpp
    Rules.cpp
    ParsedScriptCache.cpp
    ScriptSystem.cpp
    Strings.cpp
    TwoDACache.cpp
)

target_link_libraries(nw-kernel PRIVATE
    nw-base
    nw-libraries
    nw-objects
    nw-formats
    nw-i18n
)

if(ROLLNW_BUILD_RUNTIME_SCRIPTING)
target_link_libraries(nw-kernel PRIVATE Luau.VM)
endif()
