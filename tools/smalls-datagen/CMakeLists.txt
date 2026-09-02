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

# The datagen runtime loads package code during profile startup. Keep those
# packages beside the executable so invocation is independent of the process
# working directory.
file(GLOB_RECURSE SMALLS_DATAGEN_STDLIB_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/core/*"
    "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/nwn1/*"
)
set(SMALLS_DATAGEN_STDLIB_COPY_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/smalls_datagen_stdlib.stamp")
add_custom_command(
    OUTPUT "${SMALLS_DATAGEN_STDLIB_COPY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:smalls-datagen>/stdlib
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:smalls-datagen>/stdlib/core
        $<TARGET_FILE_DIR:smalls-datagen>/stdlib/nwn1
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/core"
        $<TARGET_FILE_DIR:smalls-datagen>/stdlib/core
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/nwn1"
        $<TARGET_FILE_DIR:smalls-datagen>/stdlib/nwn1
    COMMAND ${CMAKE_COMMAND} -E touch
        "${SMALLS_DATAGEN_STDLIB_COPY_STAMP}"
    DEPENDS ${SMALLS_DATAGEN_STDLIB_FILES}
    VERBATIM
)
add_custom_target(smalls-datagen-copy-stdlib
    DEPENDS "${SMALLS_DATAGEN_STDLIB_COPY_STAMP}")
add_dependencies(smalls-datagen smalls-datagen-copy-stdlib)

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

install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/core/"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/stdlib/core)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/nwn1/"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/stdlib/nwn1)
