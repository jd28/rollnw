add_library(nw-formats STATIC
    Dialog.cpp
    Faction.cpp
    Ini.cpp
    Journal.cpp
    TwoDA.cpp
)

target_link_libraries(nw-formats PRIVATE
    nw-base
    nw-libraries
)
