add_library(nw-i18n STATIC
    conversion.cpp
    Language.cpp
    LocString.cpp
    Tlk.cpp
)

target_include_directories(nw-i18n SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nw-i18n PRIVATE
    nw-base
    nw-libraries
)
