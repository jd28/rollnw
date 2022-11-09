add_library(nw-serialization STATIC
    Archives.cpp
    gff_common.cpp
    Gff.cpp
    GffBuilder.cpp
    Serialization.cpp
)

target_link_libraries(nw-serialization PRIVATE
    nw-base
    nw-libraries
)
