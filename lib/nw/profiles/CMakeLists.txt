add_library(nw-profiles STATIC
    nwn1/constants.cpp
    nwn1/modifiers.cpp
    nwn1/Profile.cpp
    nwn1/rules.cpp
    nwn1/rule_helper_funcs.cpp
)

target_link_libraries(nw-profiles PRIVATE
    nw-base
    nw-libraries
    nw-objects
    nw-rules
)
