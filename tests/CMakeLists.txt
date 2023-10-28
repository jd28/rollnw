find_package(Threads)
find_package(GTest CONFIG REQUIRED)

add_custom_target(copy-files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/test_data
    ${PROJECT_SOURCE_DIR}/bin/test_data
)

add_custom_target(copy-files2 ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/test_data
    ${CMAKE_CURRENT_BINARY_DIR}/test_data
)

# Need .cpp extension for ctest
add_executable(rollnw_test
    main.cpp

    formats_dialog.cpp
    formats_faction.cpp
    formats_ini.cpp
    #formats_journal.cpp
    formats_twoda.cpp

    kernel_config.cpp
    kernel_effect_system.cpp
    kernel_load_module.cpp
    kernel_objects.cpp
    kernel_resources.cpp
    kernel_rules.cpp
    kernel_strings.cpp
    kernel_twoda_cache.cpp

    legacy_image.cpp
    legacy_locstring.cpp
    legacy_palette.cpp
    legacy_plt.cpp
    legacy_tlk.cpp

    model_parse.cpp

    objects_area.cpp
    objects_creature.cpp
    objects_door.cpp
    objects_encounter.cpp
    objects_item.cpp
    objects_module.cpp
    objects_placeable.cpp
    objects_player.cpp
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

    rules_effects.cpp
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
    GTest::gtest)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test PRIVATE dl)
endif()

include(GoogleTest)
gtest_discover_tests(rollnw_test)
