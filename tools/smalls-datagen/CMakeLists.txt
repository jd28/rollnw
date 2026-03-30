include(GNUInstallDirs)
find_package(Threads)

add_executable(smalls-datagen main.cpp)

target_include_directories(smalls-datagen PRIVATE ../../lib)

# Copy specs directory next to the binary so the default --specs path works.
add_custom_command(TARGET smalls-datagen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/specs"
        "$<TARGET_FILE_DIR:smalls-datagen>/specs"
    COMMENT "Copying smalls-datagen specs"
)

target_link_libraries(smalls-datagen PRIVATE nw Threads::Threads)

if(CMAKE_HOST_UNIX)
    target_link_libraries(smalls-datagen PRIVATE dl)
endif()

install(TARGETS smalls-datagen
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY specs/
    DESTINATION ${CMAKE_INSTALL_BINDIR}/specs
    FILES_MATCHING PATTERN "*.json"
)
