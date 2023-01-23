add_library(nw-legacy STATIC
    gff_common.cpp
    gff_conversion.cpp
    Gff.cpp
    GffBuilder.cpp
    Image.cpp
    Palette.cpp
)

target_link_libraries(nw-legacy PRIVATE
    nw-base
    nw-libraries
)
