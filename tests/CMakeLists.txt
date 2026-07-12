find_package(Threads)

add_custom_target(copy-files2 ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/test_data
    ${CMAKE_CURRENT_BINARY_DIR}/test_data

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/nw/smalls/scripts
    ${CMAKE_CURRENT_BINARY_DIR}/stdlib
)

# Need .cpp extension for ctest
add_executable(rollnw_test
    main.cpp
    nwn1_test_builders.cpp

    formats_dialog.cpp
    formats_faction.cpp
    formats_ini.cpp
    #formats_journal.cpp
    formats_twoda.cpp

    kernel_factions.cpp
    kernel_load_module.cpp
    kernel_events.cpp
    kernel_models.cpp
    kernel_objects.cpp
    kernel_rules.cpp
    kernel_strings.cpp
    kernel_tilesets.cpp
    kernel_twoda_cache.cpp

    legacy_image.cpp
    legacy_locstring.cpp
    legacy_palette.cpp
    legacy_plt.cpp
    legacy_tlk.cpp

    model_parse.cpp
    model_particles.cpp
    ../tools/mudl/benchmark_report.cpp
    ../tools/mudl/mudl_cli.cpp
    mudl_benchmark_report.cpp
    mudl_cli.cpp
    nwn1_gff_propset_component_json.cpp

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

    rules_effects.cpp
    rules_qualifier.cpp
    rules_random.cpp
    rules_requirement.cpp
    rules_runtime_object.cpp

    render_particles.cpp

    script_nss.cpp

    serial_gff.cpp

    smalls_heap.cpp
    smalls_bytecode.cpp
    smalls_bytecode_verifier.cpp
    smalls_compiler.cpp
    smalls_gc.cpp
    smalls_diagnostics.cpp
    smalls_lexer.cpp
    smalls_parser.cpp
    smalls_parser_decl.cpp
    smalls_parser_expr.cpp
    smalls_parser_fixed_array.cpp
    smalls_parser_import.cpp
    smalls_parser_init.cpp
    smalls_parser_stmt.cpp
    smalls_parser_types.cpp
    smalls_resolver.cpp
    smalls_resolver_array.cpp
    smalls_resolver_types.cpp
    smalls_lsp.cpp
    smalls_lsp_text.cpp
    smalls_runtime.cpp
    smalls_const_eval.cpp
    smalls_corpus.cpp
    smalls_engine_integration.cpp
    smalls_modules.cpp
    smalls_virtual_machine.cpp
    smalls_config.cpp
    smalls_propset_parity.cpp
    smalls_vm_reentrant.cpp

    util_containers.cpp
    util_error_context.cpp
    util_hndlptrmap.cpp
    util_memory.cpp
    util_string.cpp
    util_tokenizer.cpp
    formats_txi.cpp
)

add_dependencies(rollnw_test copy-files2)

set_target_properties(rollnw_test PROPERTIES
    INSTALL_RPATH "$ORIGIN"
)

if(UNIX)
    set_target_properties(rollnw_test PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
    )
endif()


if(TARGET nw-render-nwn)
    target_sources(rollnw_test PRIVATE
        render_asset_cache.cpp
        render_model_instance.cpp
        render_model_loader.cpp
    )
    target_link_libraries(rollnw_test PRIVATE
        nw-render-nwn)
endif()

if(TARGET nw-render)
    target_sources(rollnw_test PRIVATE
        render_gfx_bindless.cpp
        render_frame_storage_arena.cpp
        render_model_asset.cpp
        render_pipeline_cache.cpp
        render_shader_includes.cpp
    )
    target_link_libraries(rollnw_test PRIVATE
        nw-render)
endif()

if(TARGET nw-render-viewer)
    target_sources(rollnw_test PRIVATE
        mudl_app_runtime.cpp
        render_gltf_import.cpp
        render_prepared_model_draws.cpp
        render_preview_model_animation.cpp
        render_forward_plus.cpp
        render_viewer_prepared_draws.cpp
        render_viewer_particles.cpp
        render_viewer_lighting.cpp
    )
    target_link_libraries(rollnw_test PRIVATE
        nw-render-viewer)
endif()

target_include_directories(rollnw_test PRIVATE
    ../lib
    ../lib/nw
    ../tools/mudl
    ../tools/smalls-lsp
)

target_link_libraries(rollnw_test PRIVATE
    nw
    Threads::Threads
    GTest::gtest)

if(CMAKE_HOST_UNIX)
    target_link_libraries(rollnw_test PRIVATE dl)
endif()

include(GoogleTest)
gtest_discover_tests(rollnw_test DISCOVERY_MODE PRE_TEST)

install(TARGETS rollnw_test)
install(DIRECTORY test_data/ DESTINATION bin/test_data)
