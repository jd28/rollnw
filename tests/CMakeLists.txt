find_package(Threads)
find_package(Catch2 CONFIG REQUIRED)

file(GLOB_RECURSE COPY_FILES
                  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
                  test_data/*)

foreach(FILENAME ${COPY_FILES})
    set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/${FILENAME}")
    set(DST "${CMAKE_CURRENT_BINARY_DIR}/${FILENAME}")

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

# Need .cpp extension for ctest
add_executable(rollnw_test
    main.cpp

    formats_dialog.cpp
    formats_faction.cpp
    formats_image.cpp
    formats_ini.cpp
    #formats_journal.cpp
    formats_palette.cpp
    formats_twoda.cpp

    i18n_locstring.cpp
    i18n_tlk.cpp

    kernel_config.cpp
    kernel_load_module.cpp
    kernel_objects.cpp
    kernel_resources.cpp
    kernel_rules.cpp
    kernel_strings.cpp

    model_parse.cpp

    objects_area.cpp
    objects_creature.cpp
    objects_door.cpp
    objects_encounter.cpp
    objects_item.cpp
    objects_module.cpp
    objects_placeable.cpp
    objects_sound.cpp
    objects_store.cpp
    objects_trigger.cpp
    objects_waypoint.cpp

    resources.cpp
    resources_dir.cpp
    resources_erf.cpp
    resources_key.cpp
    resources_nwsync.cpp
    resources_resource.cpp
    resources_zip.cpp

    rules_qualifier.cpp
    rules_random.cpp
    rules_requirement.cpp
    rules_modifier.cpp
    rules_selector.cpp

    script_nss.cpp

    serial_gff.cpp

    util_string.cpp
    util_tokenizer.cpp
)

target_include_directories(rollnw_test PRIVATE
    ../lib
    ../profiles
)

target_link_libraries(rollnw_test PRIVATE
    nw
    nw-profiles
    Threads::Threads
    Catch2::Catch2)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test PRIVATE dl)
endif()

include(Catch)
catch_discover_tests(rollnw_test)
