add_library(nw-components STATIC
    Area.cpp
    Creature.cpp
    Door.cpp
    Encounter.cpp
    Item.cpp
    Module.cpp
    ObjectBase.cpp
    Placeable.cpp
    Sound.cpp
    Store.cpp
    Trigger.cpp
    Waypoint.cpp

    Appearance.cpp
    CombatInfo.cpp
    Common.cpp
    CreatureStats.cpp
    Equips.cpp
    Inventory.cpp
    LevelStats.cpp
    LocalData.cpp
    Location.cpp
    Lock.cpp
    Saves.cpp
    SpellBook.cpp
    Trap.cpp
)

target_include_directories(nw-components SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-components PRIVATE
    nw-base
    nw-libraries
)
