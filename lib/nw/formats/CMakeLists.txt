add_library(nw-formats STATIC
    Dialog.cpp
    Faction.cpp
    Ini.cpp
    Journal.cpp
    Palette.cpp
    TwoDA.cpp
    Image.cpp
)

target_include_directories(nw-formats SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-formats PRIVATE
    nw-base
    nw-libraries
)
