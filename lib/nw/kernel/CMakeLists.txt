add_library(nw-kernel STATIC
    Kernel.cpp
    Config.cpp
    Objects.cpp
    Resources.cpp
    Strings.cpp
)

target_include_directories(nw-kernel SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-kernel PRIVATE
    nw-base
    nw-libraries
    nw-objects
)
