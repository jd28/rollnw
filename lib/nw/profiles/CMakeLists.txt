add_library(nw-profiles STATIC
    nwn1/Profile.cpp
)

target_link_libraries(nw-profiles PRIVATE
    nw-base
    nw-libraries
    nw-objects
    nw-rules
)
