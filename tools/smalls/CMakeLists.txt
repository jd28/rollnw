include(GNUInstallDirs)
find_package(Threads)

add_executable(smalls main.cpp)

target_include_directories(smalls PRIVATE ../../lib)

target_link_libraries(smalls PRIVATE nw Threads::Threads)

if(CMAKE_HOST_UNIX)
    target_link_libraries(smalls PRIVATE dl)
endif()

install(TARGETS smalls
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/
    DESTINATION ${CMAKE_INSTALL_BINDIR}/stdlib
    FILES_MATCHING
        PATTERN "*.smalls"
        PATTERN "package.json"
)
