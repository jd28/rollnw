find_package(Threads)
find_package(Catch2 CONFIG REQUIRED)

file(GLOB_RECURSE COPY_FILES
                  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
                  test_data/*)

foreach(FILENAME ${COPY_FILES})
    set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/${FILENAME}")
    set(DST "${CMAKE_SOURCE_DIR}/bin/${FILENAME}")

    add_custom_command(
        OUTPUT ${DST}
        DEPENDS ${SRC}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SRC} ${DST}
    )
endforeach(FILENAME)

add_custom_target(
  export-test-files ALL
  DEPENDS ${COPY_FILES}
  COMMENT "Exporting files into build tree"
)

include(CTest)
enable_testing()
# Load and use the .cmake file provided by Catch so all the test cases
# are made available to CTest.
include(ParseAndAddCatchTests)

# Need .cpp extension for ctest
add_executable(libnw_test
    main.cpp

    formats_gff.cpp
    formats_image.cpp
    formats_ini.cpp
    formats_nss.cpp
    formats_twoda.cpp

    i18n_tlk.cpp
    i18n_tlktable.cpp

    objects_dialog.cpp
    objects_door.cpp
    objects_item.cpp
    objects_palette.cpp
    objects_placeable.cpp

    resources.cpp
    resources_dir.cpp
    resources_erf.cpp
    resources_key.cpp
    resources_nwsync.cpp

    util_string.cpp
    util_tokenizer.cpp
)

set_target_properties(libnw_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_include_directories(libnw_test PRIVATE ../lib)

target_link_libraries(libnw_test PRIVATE
    nw
    Threads::Threads
    Catch2::Catch2)

if(CMAKE_HOST_UNIX)
    target_link_libraries(libnw_test PRIVATE dl)
endif()

ParseAndAddCatchTests(libnw_test)
