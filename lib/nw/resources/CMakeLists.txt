add_library(nw-resources STATIC
    Bif.cpp
    Container.cpp
    Directory.cpp
    Erf.cpp
    Key.cpp
    NWSync.cpp
    Resref.cpp
    Resource.cpp
    ResourceDescriptor.cpp
    Zip.cpp
)

target_include_directories(nw-resources SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-resources PRIVATE
    nw-base
    nw-libraries
)
