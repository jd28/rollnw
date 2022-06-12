add_library(nw-rules STATIC
    Alignment.cpp
    BaseItem.cpp
    Effect.cpp
    Feat.cpp
    Index.cpp
    system.cpp
)

target_include_directories(nw-rules SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-rules PRIVATE
    nw-base
    nw-libraries
    nw-objects
)
