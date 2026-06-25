add_library(mudl-imgui STATIC
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/imgui.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/backends/imgui_impl_sdl3.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/backends/imgui_impl_vulkan.cpp
)

target_include_directories(mudl-imgui SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7
    ${CMAKE_SOURCE_DIR}/external/imgui-1.92.7/backends
)

target_include_directories(mudl-imgui SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/nw/gfx/backends/vulkan
)

target_compile_definitions(mudl-imgui PUBLIC
    IMGUI_IMPL_VULKAN_USE_VOLK
)

target_compile_options(mudl-imgui PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-old-style-cast>
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-conversion>
)

target_link_libraries(mudl-imgui PUBLIC
    SDL3::SDL3
    nw-gfx
)

# mudl - Model Viewer & Asset Pipeline Tool

add_executable(mudl
    main.cpp
    app_runtime.cpp
    benchmark_report.cpp
    imgui_runtime.cpp
    mudl_cli.cpp
    mudl_commands.cpp
    particle_tools.cpp
    progfx.cpp
    stock_spell_fx.cpp
    visual_corpus.cpp
    viewer_runtime.cpp
)

target_include_directories(mudl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

target_include_directories(mudl SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/glm
)

target_link_libraries(mudl PRIVATE
    mudl-imgui
    SDL3::SDL3
    nw
    nw-gfx
    nw-render
    nw-render-gltf
    nw-render-nwn
    nw-render-ozz
    nw-render-viewer
)

if(UNIX)
    set_target_properties(mudl PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_RPATH_USE_ORIGIN TRUE
    )
endif()

if(ROLLNW_DXC_RUNTIME_LIBS)
    install(FILES ${ROLLNW_DXC_RUNTIME_LIBS} DESTINATION bin)
endif()

# Platform-specific system libs
if(UNIX AND NOT APPLE)
    target_link_libraries(mudl PRIVATE dl pthread)
endif()

# Copy shaders to build directory. Keep this as a dependency target so editing
# only HLSL updates the runtime shader copies even when the executable is current.
file(GLOB MUDL_RENDER_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/lib/nw/render/shaders/*.hlsl"
)
set(MUDL_SHADER_COPY_STAMP "${CMAKE_CURRENT_BINARY_DIR}/mudl_shaders.stamp")
add_custom_command(
    OUTPUT "${MUDL_SHADER_COPY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/lib/nw/render/shaders
        $<TARGET_FILE_DIR:mudl>/shaders
    COMMAND ${CMAKE_COMMAND} -E touch
        "${MUDL_SHADER_COPY_STAMP}"
    DEPENDS ${MUDL_RENDER_SHADER_FILES}
    VERBATIM
)
add_custom_target(mudl-copy-shaders DEPENDS "${MUDL_SHADER_COPY_STAMP}")
add_dependencies(mudl mudl-copy-shaders)

if(ROLLNW_BUNDLED_SDL AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET mudl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared>
            $<TARGET_FILE_DIR:mudl>
    )
endif()

# Install
install(TARGETS mudl RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/nw/render/shaders/ DESTINATION bin/shaders)

if(ROLLNW_BUNDLED_SDL AND TARGET SDL3::SDL3-shared)
    install(FILES $<TARGET_FILE:SDL3::SDL3-shared> DESTINATION bin)
endif()
