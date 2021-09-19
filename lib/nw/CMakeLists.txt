find_package(fmt CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(nw STATIC
    log

    formats/Ini
    formats/TwoDA
    formats/Image

    i18n/LocString

    resources/Bif
    resources/Container
    resources/Directory
    resources/Erf
    resources/Key
    resources/NWSync
    resources/Resref
    resources/Resource
    resources/ResourceDescriptor

    util/ByteArray
    util/compression
    util/platform
    util/string
)

target_link_libraries(nw PUBLIC
    fmt::fmt
    absl::base
    absl::flat_hash_map
    absl::hash
    absl::span
    absl::utility
    absl::strings
    nlohmann_json nlohmann_json::nlohmann_json
    ZLIB::ZLIB
    zstd::libzstd_static
    unofficial::sqlite3::sqlite3

    stb_image
    inih
)
