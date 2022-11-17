add_library(nw-rules STATIC
    attributes.cpp
    feats.cpp
    items.cpp

    Class.cpp
    Dice.cpp
    Effect.cpp
    Spell.cpp
    system.cpp
)

target_link_libraries(nw-rules PRIVATE
    nw-base
    nw-libraries
    nw-components
)
