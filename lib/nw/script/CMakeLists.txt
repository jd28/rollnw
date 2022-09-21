add_library(nw-script STATIC
    Nss.cpp
    NssLexer.cpp
    NssParser.cpp
)

target_include_directories(nw-script SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-script PRIVATE
    nw-base
    nw-libraries
)
