find_package(Threads)

add_library(nw STATIC
    functions.cpp
    log.cpp

    formats/Dialog.cpp
    formats/Faction.cpp
    formats/Image.cpp
    formats/Ini.cpp
    formats/Journal.cpp
    formats/Palette.cpp
    formats/Plt.cpp
    formats/TwoDA.cpp

    i18n/conversion.cpp
    i18n/Language.cpp
    i18n/LocString.cpp
    i18n/Tlk.cpp

    kernel/Kernel.cpp
    kernel/Config.cpp
    kernel/EffectSystem.cpp
    kernel/FactionSystem.cpp
    kernel/ModelCache.cpp
    kernel/EventSystem.cpp
    kernel/Objects.cpp
    kernel/Memory.cpp
    kernel/Resources.cpp
    kernel/Rules.cpp
    kernel/Strings.cpp
    kernel/TilesetRegistry.cpp
    kernel/TwoDACache.cpp

    model/Mdl.cpp
    model/MdlTextParser.cpp
    model/MdlBinaryParser.cpp

    # Objects
    objects/Area.cpp
    objects/Creature.cpp
    objects/Door.cpp
    objects/Encounter.cpp
    objects/Item.cpp
    objects/Module.cpp
    objects/ObjectBase.cpp
    objects/ObjectHandle.cpp
    objects/Placeable.cpp
    objects/Player.cpp
    objects/Sound.cpp
    objects/Store.cpp
    objects/Trigger.cpp
    objects/Waypoint.cpp
    # Components
    objects/Appearance.cpp
    objects/CombatInfo.cpp
    objects/Common.cpp
    objects/CreatureStats.cpp
    objects/EffectArray.cpp
    objects/Equips.cpp
    objects/Inventory.cpp
    objects/LevelHistory.cpp
    objects/LevelStats.cpp
    objects/LocalData.cpp
    objects/Location.cpp
    objects/Lock.cpp
    objects/Saves.cpp
    objects/SpellBook.cpp
    objects/Trap.cpp

    profiles/nwn1/constants.cpp
    profiles/nwn1/effects.cpp
    profiles/nwn1/functions.cpp
    profiles/nwn1/Profile.cpp
    profiles/nwn1/rules.cpp

    resources/Bif.cpp
    resources/Container.cpp
    resources/Directory.cpp
    resources/StaticDirectory.cpp
    resources/Erf.cpp
    resources/Key.cpp
    resources/NWSync.cpp
    resources/Resref.cpp
    resources/Resource.cpp
    resources/ResourceData.cpp
    resources/ResourceDescriptor.cpp
    resources/Zip.cpp

    rules/attributes.cpp
    rules/Class.cpp
    rules/combat.cpp
    rules/Dice.cpp
    rules/Effect.cpp
    rules/feats.cpp
    rules/items.cpp
    rules/Spell.cpp
    rules/system.cpp

    script/Ast.cpp
    script/Context.cpp
    script/Nss.cpp
    script/NssLexer.cpp
    script/NssParser.cpp

    serialization/gff_common.cpp
    serialization/gff_conversion.cpp
    serialization/Gff.cpp
    serialization/GffBuilder.cpp
    serialization/Serialization.cpp

    util/Allocator.cpp
    util/base64.cpp
    util/ByteArray.cpp
    util/compression.cpp
    util/game_install.cpp
    util/memory.cpp
    util/platform.cpp
    util/string.cpp
    util/Tokenizer.cpp

)

target_compile_definitions(nw PUBLIC -DGLM_ENABLE_EXPERIMENTAL)

target_link_libraries(nw PUBLIC
    fmt::fmt
    absl::base
    absl::flat_hash_map
    absl::hash
    absl::span
    absl::utility
    absl::strings
    minizip
    zlibstatic
    nowide::nowide
    libzstd_static
    sqlite3
    Threads::Threads

    stb_image
    fzy
    inih

    coverage_config
)

if(NOT LINUX)
target_link_libraries(nw PUBLIC
    iconv
)
endif()

include(../../cmake/ProjectSettings.cmake)

include(../../cmake/CompilerWarnings.cmake)
set_project_warnings(nw)

include(../../cmake/Sanitizers.cmake)
enable_sanitizers(nw)
