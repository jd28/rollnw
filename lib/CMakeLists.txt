find_package(fmt CONFIG REQUIRED)

add_library(nw STATIC
    log
)

target_link_libraries(nw PUBLIC
    fmt::fmt
)
