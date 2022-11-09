add_library(nw-kernel STATIC
    Kernel.cpp
    Config.cpp
    EffectSystem.cpp
    EventSystem.cpp
    Objects.cpp
    Resources.cpp
    Rules.cpp
    ParsedScriptCache.cpp
    Strings.cpp
    TwoDACache.cpp
)

target_link_libraries(nw-kernel PRIVATE
    nw-base
    nw-libraries
    nw-components
    nw-formats
    nw-i18n
)
