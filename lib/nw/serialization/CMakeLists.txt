add_library(nw-serialization STATIC
    Archives.cpp
    gff_common.cpp
    Gff.cpp
    GffBuilder.cpp
    Serialization.cpp
)

target_include_directories(nw-serialization SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-serialization PRIVATE
    nw-base
    nw-libraries
)
