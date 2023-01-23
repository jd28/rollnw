add_library(nw-serialization STATIC
    Serialization.cpp
)

target_link_libraries(nw-serialization PRIVATE
    nw-base
    nw-libraries
)
