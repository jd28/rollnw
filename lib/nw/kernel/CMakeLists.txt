add_library(nw-kernel STATIC
    components/ConstantRegistry.cpp
    components/TwoDACache.cpp

    Kernel.cpp
    Config.cpp
    Objects.cpp
    Resources.cpp
    Rules.cpp
    Strings.cpp
)

target_link_libraries(nw-kernel PRIVATE
    nw-base
    nw-libraries
    nw-objects
    nw-formats
    nw-i18n
)
