add_library(nw-objects STATIC
    Area.cpp
    Creature.cpp
    Door.cpp
    Encounter.cpp
    Item.cpp
    Module.cpp
    ObjectBase.cpp
    ObjectHandle.cpp
    Placeable.cpp
    Player.cpp
    Sound.cpp
    Store.cpp
    Trigger.cpp
    Waypoint.cpp

    Appearance.cpp
    CombatInfo.cpp
    Common.cpp
    CreatureStats.cpp
    EffectArray.cpp
    Equips.cpp
    Inventory.cpp
    LevelHistory.cpp
    LevelStats.cpp
    LocalData.cpp
    Location.cpp
    Lock.cpp
    Saves.cpp
    SpellBook.cpp
    Trap.cpp
)

target_link_libraries(nw-objects PRIVATE
    nw-base
    nw-libraries
)
