find_package(fmt CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

add_library(nw STATIC
    log

    util/string
)

target_link_libraries(nw PUBLIC
    fmt::fmt
    absl::base
    absl::container
    absl::flat_hash_map
    absl::hash
    absl::utility
    absl::strings
)
