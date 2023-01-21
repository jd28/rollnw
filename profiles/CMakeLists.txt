add_library(nw-profiles STATIC
    nwn1/combat.cpp
    nwn1/constants.cpp
    nwn1/effects.cpp
    nwn1/functions.cpp
    nwn1/helpers.cpp
    nwn1/Profile.cpp
    nwn1/modifiers.cpp
    nwn1/rules.cpp
)

target_link_libraries(nw-profiles PRIVATE
    nw
)

target_include_directories(nw-profiles PUBLIC
    ../lib
    ../external
)
