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

target_link_libraries(nw-resources PRIVATE
    nw-base
    nw-libraries
)
