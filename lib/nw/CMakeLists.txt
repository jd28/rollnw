find_package(Threads)
find_package(absl CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Iconv REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nowide CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_library(nw STATIC
    log

    formats/Gff
    formats/gff_conversions
    formats/Ini
    formats/Nss
    formats/NssLexer
    formats/NssParser
    formats/TwoDA
    formats/Image

    i18n/conversion
    i18n/Language
    i18n/LocString
    i18n/Tlk
    i18n/TlkTable

    objects/Object
    objects/Creature
    objects/Dialog
    objects/Door
    objects/Encounter
    objects/Faction
    objects/Item
    objects/Journal
    objects/Module
    objects/Palette
    objects/Placeable
    objects/SituatedObject
    objects/Sound
    objects/Store
    objects/Trigger
    objects/Waypoint

    objects/components/LocalData
    objects/components/Location
    objects/components/Lock
    objects/components/Trap

    resources/Bif
    resources/Container
    resources/Directory
    resources/Erf
    resources/Key
    resources/NWSync
    resources/Resref
    resources/Resource
    resources/ResourceDescriptor

    util/base64
    util/ByteArray
    util/compression
    util/platform
    util/string
    util/Tokenizer
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
