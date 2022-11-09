add_library(nw-script STATIC
    Nss.cpp
    NssLexer.cpp
    NssParser.cpp
)

target_link_libraries(nw-script PRIVATE
    nw-base
    nw-libraries
)
