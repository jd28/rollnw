add_library(nw-model STATIC
    Mdl.cpp
    MdlTextParser.cpp
)

target_link_libraries(nw-model PRIVATE
    nw-base
    nw-libraries
)
