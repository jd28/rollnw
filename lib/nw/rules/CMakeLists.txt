add_library(nw-rules STATIC
    Alignment.cpp
    ArmorClass.cpp
    BaseItem.cpp
    Class.cpp
    Dice.cpp
    Effect.cpp
    Feat.cpp
    MasterFeat.cpp
    Poison.cpp
    Race.cpp
    Skill.cpp
    Spell.cpp
    system.cpp
)

target_link_libraries(nw-rules PRIVATE
    nw-base
    nw-libraries
    nw-components
)
