find_package(Threads)
find_package(absl CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Iconv REQUIRED)
find_package(minizip CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nowide CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_library(nw-libraries INTERFACE)
target_link_libraries(nw-libraries INTERFACE
    fmt::fmt
    absl::base
    absl::flat_hash_map
    absl::hash
    absl::span
    absl::utility
    absl::strings
    glm::glm
    minizip::minizip
    nlohmann_json nlohmann_json::nlohmann_json
    nowide::nowide
    ZLIB::ZLIB
    zstd::libzstd_static
    unofficial::sqlite3::sqlite3
    Threads::Threads

    stb_image
    inih

    coverage_config
)

add_library(nw-base INTERFACE)

if(NOT Iconv_IS_BUILT_IN)
    target_link_libraries(nw-libraries INTERFACE Iconv::Iconv)
endif()

if(ROLLNW_ENABLE_LEGACY)
set(LEGACY_SRC
    legacy/gff_common.cpp
    legacy/gff_conversion.cpp
    legacy/Gff.cpp
    legacy/GffBuilder.cpp
    legacy/Image.cpp
    legacy/LocString.cpp
    legacy/Palette.cpp
    legacy/Plt.cpp
    legacy/Tlk.cpp
)
else()
set(LEGACY_SRC )
endif()

add_library(nw STATIC
    functions.cpp
    log.cpp

    formats/Dialog.cpp
    formats/Faction.cpp
    formats/Ini.cpp
    formats/Journal.cpp
    formats/TwoDA.cpp

    i18n/conversion.cpp
    i18n/Language.cpp

    kernel/Kernel.cpp
    kernel/Config.cpp
    kernel/EffectSystem.cpp
    kernel/EventSystem.cpp
    kernel/Objects.cpp
    kernel/Resources.cpp
    kernel/Rules.cpp
    kernel/ParsedScriptCache.cpp
    kernel/ScriptSystem.cpp
    kernel/Strings.cpp
    kernel/TwoDACache.cpp

    ${LEGACY_SRC}

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

    resources/Bif.cpp
    resources/Container.cpp
    resources/Directory.cpp
    resources/Erf.cpp
    resources/Key.cpp
    resources/NWSync.cpp
    resources/Resref.cpp
    resources/Resource.cpp
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

    script/Nss.cpp
    script/NssLexer.cpp
    script/NssParser.cpp

    serialization/Serialization.cpp

    util/base64.cpp
    util/ByteArray.cpp
    util/compression.cpp
    util/game_install.cpp
    util/platform.cpp
    util/string.cpp
    util/Tokenizer.cpp

)

target_link_libraries(nw PUBLIC
    nw-base
    nw-libraries
)

if(ROLLNW_BUILD_RUNTIME_SCRIPTING)
target_link_libraries(nw PUBLIC
    Luau.VM
)
endif()

include(../../cmake/ProjectSettings.cmake)

include(../../cmake/CompilerWarnings.cmake)
set_project_warnings(nw-base)

include(../../cmake/Sanitizers.cmake)
enable_sanitizers(nw-base)
