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

add_library(nw STATIC
    log.cpp

    formats/Ini.cpp
    formats/Nss.cpp
    formats/NssLexer.cpp
    formats/NssParser.cpp
    formats/TwoDA.cpp
    formats/Image.cpp

    i18n/conversion.cpp
    i18n/Language.cpp
    i18n/LocString.cpp
    i18n/Tlk.cpp

    kernel/Kernel.cpp
    kernel/Strings.cpp

    objects/Area.cpp
    objects/Creature.cpp
    objects/Dialog.cpp
    objects/Door.cpp
    objects/Encounter.cpp
    objects/Faction.cpp
    objects/Item.cpp
    objects/Journal.cpp
    objects/Module.cpp
    objects/ObjectBase.cpp
    objects/Palette.cpp
    objects/Placeable.cpp
    objects/Sound.cpp
    objects/Store.cpp
    objects/Trigger.cpp
    objects/Waypoint.cpp

    objects/components/Appearance.cpp
    objects/components/CombatInfo.cpp
    objects/components/Common.cpp
    objects/components/CreatureStats.cpp
    objects/components/Equips.cpp
    objects/components/Inventory.cpp
    objects/components/LevelStats.cpp
    objects/components/LocalData.cpp
    objects/components/Location.cpp
    objects/components/Lock.cpp
    objects/components/Saves.cpp
    objects/components/SpellBook.cpp
    objects/components/Trap.cpp

    resources/Bif.cpp
    resources/Container.cpp
    resources/Directory.cpp
    resources/Erf.cpp
    resources/Key.cpp
    resources/NWSync.cpp
    resources/Resref.cpp
    resources/Resource.cpp
    resources/ResourceDescriptor.cpp
    resources/ResourceLocator.cpp
    resources/Zip.cpp

    serialization/Archives.cpp
    serialization/gff_common.cpp
    serialization/GffInputArchive.cpp
    serialization/GffOutputArchive.cpp
    serialization/Serialization.cpp

    util/base64.cpp
    util/ByteArray.cpp
    util/compression.cpp
    util/platform.cpp
    util/string.cpp
    util/Tokenizer.cpp
)

target_link_libraries(nw PUBLIC
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
)

if(NOT Iconv_IS_BUILT_IN)
    target_link_libraries(nw PUBLIC Iconv::Iconv)
endif()

include(../../cmake/ProjectSettings.cmake)
include(../../cmake/PreventInSourceBuilds.cmake)

target_compile_features(nw INTERFACE cxx_std_17)

include(../../cmake/Cache.cmake)

include(../../cmake/CompilerWarnings.cmake)
set_project_warnings(nw)

include(../../cmake/Sanitizers.cmake)
enable_sanitizers(nw)
