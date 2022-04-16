add_library(nw-util STATIC
    base64.cpp
    ByteArray.cpp
    compression.cpp
    game_install.cpp
    platform.cpp
    string.cpp
    Tokenizer.cpp
)

target_include_directories(nw-util SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-util PRIVATE
    nw-base
    nw-libraries
)
