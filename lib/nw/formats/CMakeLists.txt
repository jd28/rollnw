add_library(nw-formats STATIC
    Ini.cpp
    Nss.cpp
    NssLexer.cpp
    NssParser.cpp
    TwoDA.cpp
    Image.cpp
)

target_include_directories(nw-formats SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-formats PRIVATE
    nw-base
    nw-libraries
)
