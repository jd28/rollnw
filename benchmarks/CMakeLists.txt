include(GNUInstallDirs)

add_custom_target(copy-benchmark-files ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory
    ${CMAKE_CURRENT_BINARY_DIR}/stdlib

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/nw/smalls/scripts
    ${CMAKE_CURRENT_BINARY_DIR}/stdlib

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/ui/scripts/toolset
    ${CMAKE_CURRENT_BINARY_DIR}/stdlib/toolset
)

add_executable(rollnw_benchmark
    appearance_catalog.cpp
    combat.cpp
    main.cpp
    particles.cpp
    propset.cpp
    smalls.cpp
    smalls_property_tree.cpp
    unmanaged_array.cpp
    ../tests/nwn1_test_builders.cpp
    ../tools/ui/smalls_property_tree.cpp
)

target_include_directories(rollnw_benchmark PRIVATE
    ../lib
    ../tests
    ../tools/ui
)
target_compile_definitions(rollnw_benchmark PRIVATE
    ROLLNW_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(rollnw_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wno-c2y-extensions>
)
target_include_directories(rollnw_benchmark SYSTEM PRIVATE ../external)

target_link_libraries(rollnw_benchmark PRIVATE
    benchmark::benchmark
    rollnw_client_core
    nw
)

add_dependencies(rollnw_benchmark copy-benchmark-files)

if(TARGET nw-render-nwn)
    target_sources(rollnw_benchmark PRIVATE
        render_nwn.cpp
    )
    target_link_libraries(rollnw_benchmark PRIVATE
        nw-render-nwn)
endif()

if(TARGET nw-render-viewer)
    target_sources(rollnw_benchmark PRIVATE
        area_object_selection.cpp
    )
    target_link_libraries(rollnw_benchmark PRIVATE
        nw-render-viewer)
endif()

if(TARGET RmlUi::RmlUi)
    target_sources(rollnw_benchmark PRIVATE
        rml_smalls.cpp
        ../tools/ui/rml_smalls_language_binding.cpp
        ../tools/ui/smalls_diagnostics.cpp
        ../tools/ui/smalls_rmlui.cpp
    )
    target_include_directories(rollnw_benchmark PRIVATE
        ../external/RmlUi-6.2/Include
    )
    target_link_libraries(rollnw_benchmark PRIVATE RmlUi::RmlUi)
endif()

install(TARGETS rollnw_benchmark
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/
    DESTINATION ${CMAKE_INSTALL_BINDIR}/stdlib
    FILES_MATCHING
        PATTERN "*.smalls"
        PATTERN "package.json"
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/tests/test_data/
    DESTINATION ${CMAKE_INSTALL_BINDIR}/test_data
)
