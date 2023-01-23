add_library(nw-i18n STATIC
    conversion.cpp
    Language.cpp
)

target_link_libraries(nw-i18n PRIVATE
    nw-base
    nw-libraries
)
