add_library(nw-objects STATIC
    Area.cpp
    Creature.cpp
    Dialog.cpp
    Door.cpp
    Encounter.cpp
    Faction.cpp
    Item.cpp
    Journal.cpp
    Module.cpp
    ObjectBase.cpp
    Palette.cpp
    Placeable.cpp
    Sound.cpp
    Store.cpp
    Trigger.cpp
    Waypoint.cpp

    components/Appearance.cpp
    components/CombatInfo.cpp
    components/Common.cpp
    components/CreatureStats.cpp
    components/Equips.cpp
    components/Inventory.cpp
    components/LevelStats.cpp
    components/LocalData.cpp
    components/Location.cpp
    components/Lock.cpp
    components/Saves.cpp
    components/SpellBook.cpp
    components/Trap.cpp
)

target_include_directories(nw-objects SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-objects PRIVATE
    nw-base
    nw-libraries
)
