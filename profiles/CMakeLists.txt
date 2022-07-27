add_library(nw-profiles STATIC
    nwn1/constants.cpp
    nwn1/constants/const_baseitem.cpp

    nwn1/functions/funcs_armor_class.cpp
    nwn1/functions/funcs_class.cpp
    nwn1/functions/funcs_combat.cpp
    nwn1/functions/funcs_feat.cpp
    nwn1/functions/funcs_skill.cpp
    nwn1/functions/rule_helper_funcs.cpp

    nwn1/modifiers.cpp
    nwn1/Profile.cpp
    nwn1/rules.cpp
)

target_link_libraries(nw-profiles PRIVATE
    nw
)

target_include_directories(nw-profiles PUBLIC
    ../lib
    ../external
)
