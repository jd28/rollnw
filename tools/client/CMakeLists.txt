add_library(rollnw_client_core STATIC
    area_map.cpp
    appearance_catalog.cpp
    command_bus.cpp
    dialog_document.cpp
    dock_layout.cpp
    forward_plus_debug.cpp
    object_document.cpp
    object_edits.cpp
    preview_session.cpp
    project.cpp
    resource_document.cpp
    script_commands.cpp
    shell_controller.cpp
    terminal.cpp
    workspace.cpp
)

target_include_directories(rollnw_client_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(rollnw_client_core PUBLIC nw nw-nav)

if(UNIX)
    target_link_libraries(rollnw_client_core PUBLIC dl)
endif()

if(NOT ROLLNW_BUILD_CLIENT)
    return()
endif()

set(RMLUI_BACKENDS_DIR "${CMAKE_SOURCE_DIR}/external/RmlUi-6.2/Backends")
set(RMLUI_INCLUDE_DIR  "${CMAKE_SOURCE_DIR}/external/RmlUi-6.2/Include")

add_executable(rollnw-client
    creature_body_part_editor.cpp
    dialog_view.cpp
    item_editor.cpp
    viewer_viewport.cpp
    main.cpp
    renderer.cpp
    toolset_backend.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/rml_smalls_bridge.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/rml_smalls_data_model.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/rml_smalls_expression_binding.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/rml_smalls_language_binding.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_diagnostics.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_creature_feats.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_creature_inventory.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_creature_properties.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_creature_spells.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_item_icons.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/rml_generated_texture.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/rml_managed_list.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_property_tree.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_rmlui.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/smalls_ui_v1.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/ui_v1.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/virtual_combobox.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/virtual_list.cpp
    ${RMLUI_BACKENDS_DIR}/RmlUi_Platform_SDL.cpp
)

target_include_directories(rollnw-client PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/mudl
    ${CMAKE_SOURCE_DIR}/tools/ui
)

# Mark RmlUi headers as SYSTEM to suppress warnings from external code
target_include_directories(rollnw-client SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/glm
    ${RMLUI_BACKENDS_DIR}
    ${RMLUI_INCLUDE_DIR}
)

target_compile_definitions(rollnw-client PRIVATE
    RMLUI_SDL_VERSION_MAJOR=3
    ROLLNW_CLIENT_APP_VERSION="${PROJECT_VERSION}"
    ROLLNW_CLIENT_APP_ID="org.rollnw.client"
)

target_sources(rollnw-client PRIVATE
  renderer_nwgfx.cpp
  rml_nwgfx_renderer.cpp
  rml_nwgfx_shaders.cpp
)
target_compile_definitions(rollnw-client PRIVATE ROLLNW_CLIENT_USE_NWGFX_BACKEND=1)

target_link_libraries(rollnw-client PRIVATE rollnw_client_core SDL3::SDL3)
target_link_libraries(rollnw-client PRIVATE nw-gfx)
target_link_libraries(rollnw-client PRIVATE
    nw-render
    nw-render-gltf
    nw-render-nwn
    nw-render-ozz
    nw-render-viewer
)

if(TARGET RmlUi::RmlUi)
    target_link_libraries(rollnw-client PRIVATE RmlUi::RmlUi)
elseif(TARGET rmlui_core)
    target_link_libraries(rollnw-client PRIVATE rmlui_core)
else()
    message(FATAL_ERROR "rollnw-client: cannot find RmlUi target")
endif()

if(UNIX)
    target_link_libraries(rollnw-client PRIVATE dl)
    set_target_properties(rollnw-client PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_RPATH_USE_ORIGIN TRUE
    )
endif()

# Copy fonts next to the binary so SDL_GetBasePath() finds them at runtime
set(ROLLNW_CLIENT_FONT_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/inter")
set(ROLLNW_CLIENT_MONO_FONT_ASSET "${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/misc/fonts/Cousine-Regular.ttf")
set(ROLLNW_CLIENT_MONO_FONT_LICENSE "${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/docs/FONTS.md")
set(ROLLNW_CLIENT_UI_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/ui")
file(GLOB ROLLNW_CLIENT_RENDER_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/lib/nw/render/shaders/*.hlsl"
)
set(ROLLNW_CLIENT_SHADER_COPY_STAMP "${CMAKE_CURRENT_BINARY_DIR}/rollnw-client_shaders.stamp")
add_custom_command(
    OUTPUT "${ROLLNW_CLIENT_SHADER_COPY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lib/nw/render/shaders"
        $<TARGET_FILE_DIR:rollnw-client>/shaders
    COMMAND ${CMAKE_COMMAND} -E touch
        "${ROLLNW_CLIENT_SHADER_COPY_STAMP}"
    DEPENDS ${ROLLNW_CLIENT_RENDER_SHADER_FILES}
    VERBATIM
)
add_custom_target(rollnw-client-copy-shaders DEPENDS "${ROLLNW_CLIENT_SHADER_COPY_STAMP}")
add_dependencies(rollnw-client rollnw-client-copy-shaders)
add_custom_target(rollnw-client-copy-stdlib
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:rollnw-client>/stdlib
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:rollnw-client>/stdlib/core
        $<TARGET_FILE_DIR:rollnw-client>/stdlib/nwn1
        $<TARGET_FILE_DIR:rollnw-client>/stdlib/toolset
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/core"
        $<TARGET_FILE_DIR:rollnw-client>/stdlib/core
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/nwn1"
        $<TARGET_FILE_DIR:rollnw-client>/stdlib/nwn1
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/tools/ui/scripts/toolset"
        $<TARGET_FILE_DIR:rollnw-client>/stdlib/toolset
    VERBATIM
)
add_dependencies(rollnw-client rollnw-client-copy-stdlib)
add_custom_target(rollnw-client-copy-ui
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:rollnw-client>/ui
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ROLLNW_CLIENT_UI_ASSETS}"
        $<TARGET_FILE_DIR:rollnw-client>/ui
    VERBATIM
)
add_dependencies(rollnw-client rollnw-client-copy-ui)
add_custom_command(TARGET rollnw-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-Regular.ttf"
        "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-Medium.ttf"
        "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-SemiBold.ttf"
        "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-Bold.ttf"
        $<TARGET_FILE_DIR:rollnw-client>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ROLLNW_CLIENT_FONT_ASSETS}/LICENSE.txt"
        $<TARGET_FILE_DIR:rollnw-client>/Inter-LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ROLLNW_CLIENT_MONO_FONT_ASSET}"
        $<TARGET_FILE_DIR:rollnw-client>/Cousine-Regular.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ROLLNW_CLIENT_MONO_FONT_LICENSE}"
        $<TARGET_FILE_DIR:rollnw-client>/Cousine-LICENSE.md
)

if(ROLLNW_BUNDLED_SDL AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET rollnw-client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared>
            $<TARGET_FILE_DIR:rollnw-client>
    )
endif()

install(TARGETS rollnw-client RUNTIME DESTINATION bin)
install(FILES
    "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-Regular.ttf"
    "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-Medium.ttf"
    "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-SemiBold.ttf"
    "${ROLLNW_CLIENT_FONT_ASSETS}/Inter-Bold.ttf"
    DESTINATION bin
)
install(FILES "${ROLLNW_CLIENT_FONT_ASSETS}/LICENSE.txt" DESTINATION bin RENAME Inter-LICENSE.txt)
install(FILES "${ROLLNW_CLIENT_MONO_FONT_ASSET}" DESTINATION bin RENAME Cousine-Regular.ttf)
install(FILES "${ROLLNW_CLIENT_MONO_FONT_LICENSE}" DESTINATION bin RENAME Cousine-LICENSE.md)
install(DIRECTORY "${ROLLNW_CLIENT_UI_ASSETS}/" DESTINATION bin/ui)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/nw/render/shaders/" DESTINATION bin/shaders)
install(CODE "
    file(REMOVE_RECURSE
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/stdlib/core\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/stdlib/nwn1\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/stdlib/toolset\"
    )
")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/core/" DESTINATION bin/stdlib/core)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/lib/nw/smalls/scripts/nwn1/" DESTINATION bin/stdlib/nwn1)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/tools/ui/scripts/toolset/" DESTINATION bin/stdlib/toolset)
