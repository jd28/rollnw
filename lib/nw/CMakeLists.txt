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

add_library(nw STATIC
    log.cpp
)

add_subdirectory(components)
add_subdirectory(formats)
add_subdirectory(i18n)
add_subdirectory(kernel)
add_subdirectory(model)
add_subdirectory(resources)
add_subdirectory(rules)
add_subdirectory(script)
add_subdirectory(serialization)
add_subdirectory(util)

target_link_libraries(nw PUBLIC
    nw-base
    nw-libraries
    nw-formats
    nw-i18n
    nw-kernel
    nw-components
    nw-model
    nw-resources
    nw-rules
    nw-script
    nw-serialization
    nw-util
)

include(../../cmake/ProjectSettings.cmake)

target_compile_features(nw-base INTERFACE cxx_std_17)

include(../../cmake/Cache.cmake)

include(../../cmake/CompilerWarnings.cmake)
set_project_warnings(nw-base)

include(../../cmake/Sanitizers.cmake)
enable_sanitizers(nw-base)
